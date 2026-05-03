// quadany-replaceable-with-derivative-uniform-branch
//
// Detects an `if (QuadAny(cond))` (or `QuadAll`) wrapper whose body has no
// derivative dependence on quad-divergent values. Per ADR 0010 §Phase 4
// (rule #28) and the SM 6.7 spec, `QuadAny`/`QuadAll` exist to keep helper
// lanes participating across a per-lane branch -- they pay a wave-shuffle
// (2-4 instructions on Turing/Ada/RDNA 2/3/Xe-HPG) for that. When the
// branch body uses no derivative-bearing op on quad-divergent values, the
// shuffle is wasted and the bare condition suffices.
//
// Stage: ControlFlow (forward-compatible-stub for Phase 4 quad-uniformity
// analysis).
//
// The full rule needs a Phase 4 quad-uniformity oracle: classify every
// expression inside the wrapped branch body as quad-uniform or
// quad-divergent, then check whether any derivative consumer depends on a
// quad-divergent value. Sub-phase 4b's wave-uniformity oracle is per-wave,
// not per-quad; the quad-level pass is queued for the same engine
// extension as the helper-lane stamping. This stub fires on the
// safe-and-loud syntactic shape: `if (QuadAny(...))` or
// `if (QuadAll(...))` whose body contains *no* derivative-bearing
// operation at all -- those are unambiguously safe to unwrap, since with
// no derivatives there is nothing for the helpers to support. The rule
// stays silent on bodies that contain derivatives, where the quad-
// uniformity oracle is needed to distinguish safe vs unsafe unwrap. That
// silent half tightens once the oracle ships.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "quadany-replaceable-with-derivative-uniform-branch";
constexpr std::string_view k_category = "wave-helper-lane";

constexpr std::array<std::string_view, 2> k_quad_wrappers{
    "QuadAny",
    "QuadAll",
};

constexpr std::array<std::string_view, 3> k_derivative_consumers{
    "ddx",
    "ddy",
    ".Sample(",
};

/// True when the condition expression on this `if`-statement is a
/// `QuadAny(...)` or `QuadAll(...)` call. Returns the wrapper-call node so
/// the diagnostic can anchor to it; null otherwise.
[[nodiscard]] ::TSNode quad_wrapper_in_condition(::TSNode if_stmt,
                                                 std::string_view bytes) noexcept {
    const ::TSNode cond = ::ts_node_child_by_field_name(if_stmt, "condition", 9);
    if (::ts_node_is_null(cond)) {
        return {};
    }
    // The condition slot holds a `condition_clause` whose `value` is the
    // actual expression. Pull through it; fall back to a textual search if
    // the field-name lookup fails for any grammar variant.
    ::TSNode expr = ::ts_node_child_by_field_name(cond, "value", 5);
    if (::ts_node_is_null(expr)) {
        // Walk the named children looking for a call_expression directly.
        const std::uint32_t named = ::ts_node_named_child_count(cond);
        for (std::uint32_t i = 0; i < named; ++i) {
            const ::TSNode child = ::ts_node_named_child(cond, i);
            if (!::ts_node_is_null(child) && node_kind(child) == "call_expression") {
                expr = child;
                break;
            }
        }
    }
    if (::ts_node_is_null(expr)) {
        return {};
    }
    // Strip parens.
    if (node_kind(expr) == "parenthesized_expression") {
        const std::uint32_t named = ::ts_node_named_child_count(expr);
        if (named >= 1) {
            expr = ::ts_node_named_child(expr, 0);
        }
    }
    if (node_kind(expr) != "call_expression") {
        return {};
    }
    const ::TSNode fn = ::ts_node_child_by_field_name(expr, "function", 8);
    if (::ts_node_is_null(fn)) {
        return {};
    }
    const auto fn_text = node_text(fn, bytes);
    for (const auto name : k_quad_wrappers) {
        if (fn_text == name) {
            return expr;
        }
    }
    return {};
}

[[nodiscard]] ::TSNode if_consequence(::TSNode if_stmt) noexcept {
    return ::ts_node_child_by_field_name(if_stmt, "consequence", 11);
}

[[nodiscard]] bool body_has_derivative_consumer(std::string_view body_text) noexcept {
    for (const auto name : k_derivative_consumers) {
        if (body_text.find(name) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "if_statement") {
        const ::TSNode wrapper = quad_wrapper_in_condition(node, bytes);
        if (!::ts_node_is_null(wrapper)) {
            const ::TSNode body = if_consequence(node);
            if (!::ts_node_is_null(body)) {
                const auto body_text = node_text(body, bytes);
                if (!body_has_derivative_consumer(body_text)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(wrapper)};
                    diag.message = std::string{
                        "`QuadAny`/`QuadAll` wraps a branch whose body does "
                        "no derivative-bearing work; the wrapper pays a "
                        "wave-shuffle without keeping helpers alive for any "
                        "derivative consumer -- the bare condition suffices "
                        "(SM 6.7 quad intrinsics)"};

                    // Unwrap `QuadAny(cond)` / `QuadAll(cond)` to the bare
                    // `cond`. The wrapper is a `call_expression` with a
                    // single argument; we replace the entire wrapper span
                    // with the inner argument's source text. The inner
                    // expression is not duplicated, so this is safe even
                    // when `cond` itself is a non-trivial expression. Since
                    // the rule already proved the body has no derivative
                    // consumers, the unwrap is semantics-preserving on the
                    // syntactic shape that fires today.
                    const ::TSNode args = ::ts_node_child_by_field_name(wrapper, "arguments", 9);
                    if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 1U) {
                        const ::TSNode inner = ::ts_node_named_child(args, 0);
                        if (!::ts_node_is_null(inner)) {
                            const auto inner_text = node_text(inner, bytes);
                            if (!inner_text.empty()) {
                                Fix fix;
                                fix.machine_applicable = true;
                                fix.description = std::string{
                                    "drop `QuadAny`/`QuadAll`; body has no "
                                    "derivative-bearing consumer to keep "
                                    "helpers alive for"};
                                TextEdit edit;
                                edit.span = Span{
                                    .source = tree.source_id(),
                                    .bytes = tree.byte_range(wrapper),
                                };
                                edit.replacement = std::string{inner_text};
                                fix.edits.push_back(std::move(edit));
                                diag.fixes.push_back(std::move(fix));
                            }
                        }
                    }

                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class QuadAnyReplaceableWithDerivativeUniformBranch : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& /*cfg*/, RuleContext& ctx) override {
        // Forward-compatible: stays on the ControlFlow dispatch path so the
        // Phase 4 quad-uniformity oracle, when it lands, can extend the
        // detection to bodies whose derivative consumers depend only on
        // quad-uniform values.
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_quadany_replaceable_with_derivative_uniform_branch() {
    return std::make_unique<QuadAnyReplaceableWithDerivativeUniformBranch>();
}

}  // namespace hlsl_clippy::rules
