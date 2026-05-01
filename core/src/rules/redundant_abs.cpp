// redundant-abs
//
// Detects three forms of `abs(x)` whose argument is provably non-negative:
//
//   - `abs(saturate(x))` -- saturate's output is in [0, 1], abs is a no-op
//   - `abs(x * x)`       -- a self-product is always >= 0 in real arithmetic
//   - `abs(dot(v, v))`   -- a self-dot is the squared magnitude, always >= 0
//
// The "self" check is purely textual: both operands of the multiply, or both
// arguments of dot, must have identical source spelling. This is conservative
// (e.g. `abs(a * b)` where `a == b` at runtime is missed) but it makes the
// fire condition unambiguous and the fix universally safe.
//
// All three forms produce a machine-applicable fix that drops the outer
// `abs(...)` wrapper, replacing the call with its sole argument's text.

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

constexpr std::string_view k_rule_id = "redundant-abs";
constexpr std::string_view k_category = "saturate-redundancy";

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

/// Returns the anonymous (operator) text of a binary_expression, or empty.
[[nodiscard]] std::string_view binary_op(::TSNode expr, std::string_view bytes) noexcept {
    const uint32_t count = ::ts_node_child_count(expr);
    for (uint32_t i = 0; i < count; ++i) {
        const ::TSNode child = ::ts_node_child(expr, i);
        if (::ts_node_is_null(child) || ::ts_node_is_named(child)) continue;
        return node_text(child, bytes);
    }
    return {};
}

/// True if `node` is a call_expression to `fn_name` with exactly one argument.
[[nodiscard]] bool is_unary_call_to(::TSNode node,
                                    std::string_view bytes,
                                    std::string_view fn_name) noexcept {
    if (node_kind(node) != "call_expression") return false;
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (node_text(fn, bytes) != fn_name) return false;
    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args)) return false;
    return ::ts_node_named_child_count(args) == 1U;
}

/// Reason describing which sub-pattern matched, for the diagnostic message.
enum class Reason : std::uint8_t {
    SaturateInside,  ///< abs(saturate(x))
    SelfProduct,     ///< abs(x * x)
    SelfDot,         ///< abs(dot(v, v))
};

/// Determine whether the (sole) argument of an `abs(...)` call is provably
/// non-negative under our three patterns. Returns the Reason if so.
[[nodiscard]] bool classify_arg(::TSNode arg, std::string_view bytes, Reason& reason_out) noexcept {
    if (::ts_node_is_null(arg)) return false;

    // abs(saturate(x))
    if (is_unary_call_to(arg, bytes, "saturate")) {
        reason_out = Reason::SaturateInside;
        return true;
    }

    // abs(dot(v, v)) -- two-arg call, args textually equal.
    if (node_kind(arg) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(arg, "function", 8);
        if (node_text(fn, bytes) == "dot") {
            const ::TSNode args = ::ts_node_child_by_field_name(arg, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 2U) {
                const ::TSNode a0 = ::ts_node_named_child(args, 0);
                const ::TSNode a1 = ::ts_node_named_child(args, 1);
                const auto t0 = node_text(a0, bytes);
                const auto t1 = node_text(a1, bytes);
                if (!t0.empty() && t0 == t1) {
                    reason_out = Reason::SelfDot;
                    return true;
                }
            }
        }
    }

    // abs(x * x) -- binary_expression with `*` and textually identical operands.
    if (node_kind(arg) == "binary_expression" && binary_op(arg, bytes) == "*") {
        const ::TSNode l = ::ts_node_child_by_field_name(arg, "left", 4);
        const ::TSNode r = ::ts_node_child_by_field_name(arg, "right", 5);
        const auto lt = node_text(l, bytes);
        const auto rt = node_text(r, bytes);
        if (!lt.empty() && lt == rt) {
            reason_out = Reason::SelfProduct;
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::string_view reason_blurb(Reason r) noexcept {
    switch (r) {
        case Reason::SaturateInside:
            return "`saturate` already clamps its result to [0, 1]";
        case Reason::SelfProduct:
            return "a self-product `x * x` is always non-negative";
        case Reason::SelfDot:
            return "`dot(v, v)` is the squared magnitude and is always non-negative";
    }
    return "the inner expression is provably non-negative";
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) return;

    if (is_unary_call_to(node, bytes, "abs")) {
        const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
        const ::TSNode arg = ::ts_node_named_child(args, 0);
        Reason reason{};
        if (classify_arg(arg, bytes, reason)) {
            const auto call_range = tree.byte_range(node);
            const auto arg_text = node_text(arg, bytes);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
            diag.message = std::string{"`abs(...)` is redundant here -- "} +
                           std::string{reason_blurb(reason)} +
                           ", so the wrapping `abs` adds an instruction with no effect";

            if (!arg_text.empty()) {
                Fix fix;
                fix.machine_applicable = true;
                fix.description = std::string{"drop the outer `abs` -- the inner expression "
                                              "is already non-negative"};
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                edit.replacement = std::string{arg_text};
                fix.edits.push_back(std::move(edit));
                diag.fixes.push_back(std::move(fix));
            }
            ctx.emit(std::move(diag));
        }
    }

    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class RedundantAbs : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return k_rule_id; }
    [[nodiscard]] std::string_view category() const noexcept override { return k_category; }
    [[nodiscard]] Stage stage() const noexcept override { return Stage::Ast; }

    void on_tree(const AstTree& tree, RuleContext& ctx) override {
        const auto bytes = tree.source_bytes();
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_redundant_abs() {
    return std::make_unique<RedundantAbs>();
}

}  // namespace hlsl_clippy::rules
