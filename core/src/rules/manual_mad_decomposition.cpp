// manual-mad-decomposition
//
// Detects hand-written `a * b + c` (multiply-add) expressions that the
// developer has explicitly parenthesised as `(a * b) + c` or `c + (a * b)`.
// HLSL's `mad(a, b, c)` intrinsic is the portable spelling of "fused
// multiply-add when the target supports it"; on GPUs without an explicit
// fused unit the compiler still treats the call as a single MAD slot in the
// scheduler, while loose `*` then `+` may be split across two issue cycles
// or fall foul of `precise` / `IEEE-strict` codegen flags that disable the
// fusion.
//
// Per ADR 0009 the safe, simple form: a `binary_expression` with operator
// `+` (or `-`, treated as `+ (-c)` style), where one operand is a
// `parenthesized_expression` wrapping a `binary_expression` with operator
// `*`. The fix is SUGGESTION-ONLY because some shaders want loose *+
// semantics for IEEE-precise reproducibility, and the developer should
// confirm that `mad` is acceptable.
//
// (The richer cross-statement `T tmp = a*b; ... tmp + c;` pattern is left
// to a follow-up rule that depends on light data-flow.)

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {

namespace {

constexpr std::string_view k_rule_id = "manual-mad-decomposition";
constexpr std::string_view k_category = "math";

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        const auto t = node_text(child, bytes);
        if (!t.empty())
            return t;
    }
    return {};
}

/// Peel a parenthesized_expression wrapper, returning the (sole) inner
/// expression node. Returns the original node if it's not parenthesised.
[[nodiscard]] ::TSNode unwrap_paren(::TSNode node) noexcept {
    while (!::ts_node_is_null(node) && node_kind(node) == "parenthesized_expression") {
        if (::ts_node_named_child_count(node) < 1U)
            break;
        node = ::ts_node_named_child(node, 0);
    }
    return node;
}

/// If `node` is `(a * b)` (parenthesised multiply), return the (a, b) pair
/// via out-params. Otherwise return false.
[[nodiscard]] bool match_paren_mul(::TSNode node,
                                   std::string_view bytes,
                                   ::TSNode& a_out,
                                   ::TSNode& b_out) noexcept {
    if (node_kind(node) != "parenthesized_expression")
        return false;
    const ::TSNode inner = unwrap_paren(node);
    if (node_kind(inner) != "binary_expression")
        return false;
    if (binary_op(inner, bytes) != "*")
        return false;
    a_out = ::ts_node_child_by_field_name(inner, "left", 4);
    b_out = ::ts_node_child_by_field_name(inner, "right", 5);
    return !::ts_node_is_null(a_out) && !::ts_node_is_null(b_out);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression" && binary_op(node, bytes) == "+") {
        const ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        const ::TSNode rhs = ::ts_node_child_by_field_name(node, "right", 5);

        ::TSNode a{};
        ::TSNode b{};
        ::TSNode c{};
        bool matched = false;
        if (match_paren_mul(lhs, bytes, a, b)) {
            c = rhs;
            matched = true;
        } else if (match_paren_mul(rhs, bytes, a, b)) {
            c = lhs;
            matched = true;
        }

        // Don't fire if the "c" operand is itself a parenthesised multiply --
        // that would produce ambiguous suggestions like `mad(a,b, (d*e))`.
        if (matched) {
            ::TSNode dummy_a{};
            ::TSNode dummy_b{};
            if (match_paren_mul(c, bytes, dummy_a, dummy_b)) {
                matched = false;
            }
        }

        if (matched) {
            const auto a_text = node_text(a, bytes);
            const auto b_text = node_text(b, bytes);
            const auto c_text = node_text(c, bytes);
            if (!a_text.empty() && !b_text.empty() && !c_text.empty()) {
                const auto expr_range = tree.byte_range(node);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
                diag.message = std::string{
                    "this is a hand-written multiply-add (`(a * b) + c`) -- prefer "
                    "`mad(a, b, c)`, which the compiler is free to schedule into a "
                    "single fused MAD slot on GPUs that support one"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{"replace `(a * b) + c` with `mad("} +
                                  std::string{a_text} + ", " + std::string{b_text} + ", " +
                                  std::string{c_text} +
                                  ")`; verify that the surrounding code does not require "
                                  "strict IEEE-precise rounding (which would forbid the fusion)";
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
                edit.replacement = std::string{"mad("} + std::string{a_text} + ", " +
                                   std::string{b_text} + ", " + std::string{c_text} + ")";
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
                // Don't double-fire on outer additions that contain this expr.
                return;
            }
        }
    }

    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualMadDecomposition : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::Ast;
    }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_mad_decomposition() {
    return std::make_unique<ManualMadDecomposition>();
}

}  // namespace hlsl_clippy::rules
