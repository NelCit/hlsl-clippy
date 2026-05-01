// manual-distance
//
// Detects `length(a - b)` and suggests `distance(a, b)`. The two are
// numerically and performance-wise equivalent on every shader compiler we
// know of -- `distance` is the named idiom and reads more clearly at the call
// site. The fix is machine-applicable.
//
// The match is syntactic: a `call_expression` named `length` whose single
// argument is a `binary_expression` with operator `-`. Parenthesised
// subtractions parse as `binary_expression` inside a `parenthesized_expression`
// in tree-sitter-hlsl; we accept that shape too by unwrapping a single layer
// of parens.

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

constexpr std::string_view k_rule_id = "manual-distance";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_length_name = "length";

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

/// Return the operator text of a binary_expression node.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const std::uint32_t count = ::ts_node_child_count(expr);
    for (std::uint32_t i = 0; i < count; ++i) {
        ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child))
            continue;
        return node_text(child, bytes);
    }
    return {};
}

/// If `node` is a parenthesized_expression with a single named child, return
/// that child; otherwise return `node` unchanged.
[[nodiscard]] ::TSNode unwrap_parens(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return node;
    if (node_kind(node) != "parenthesized_expression")
        return node;
    if (::ts_node_named_child_count(node) != 1U)
        return node;
    const ::TSNode inner = ::ts_node_named_child(node, 0);
    return ::ts_node_is_null(inner) ? node : inner;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == k_length_name) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 1U) {
                ::TSNode arg = ::ts_node_named_child(args, 0);
                arg = unwrap_parens(arg);
                if (node_kind(arg) == "binary_expression" && binary_op(arg, bytes) == "-") {
                    const ::TSNode lhs = ::ts_node_child_by_field_name(arg, "left", 4);
                    const ::TSNode rhs = ::ts_node_child_by_field_name(arg, "right", 5);
                    const auto a_text = node_text(lhs, bytes);
                    const auto b_text = node_text(rhs, bytes);
                    if (!a_text.empty() && !b_text.empty()) {
                        const auto call_range = tree.byte_range(node);

                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                        diag.message = std::string{
                            "`length(a - b)` is the open-coded form of "
                            "`distance(a, b)` -- prefer the named intrinsic for "
                            "clarity"};

                        Fix fix;
                        fix.machine_applicable = true;
                        fix.description =
                            std::string{"replace `length(a - b)` with `distance(a, b)`"};
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                        std::string replacement;
                        replacement.reserve(a_text.size() + b_text.size() + 12);
                        replacement.append("distance(");
                        replacement.append(a_text);
                        replacement.append(", ");
                        replacement.append(b_text);
                        replacement.append(")");
                        edit.replacement = std::move(replacement);
                        fix.edits.push_back(std::move(edit));
                        diag.fixes.push_back(std::move(fix));

                        ctx.emit(std::move(diag));
                    }
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class ManualDistance : public Rule {
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
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_manual_distance() {
    return std::make_unique<ManualDistance>();
}

}  // namespace hlsl_clippy::rules
