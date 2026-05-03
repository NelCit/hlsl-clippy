// select-vs-lerp-of-constant
//
// Detects `lerp(K1, K2, t)` where both K1 and K2 are numeric literals. The
// algebraic identity `lerp(K1, K2, t) == K1 + (K2 - K1) * t == mad(t, K2-K1, K1)`
// is exact, but the compiler is not guaranteed to perform the constant fold
// portably across DXIL / SPIR-V / Metal / WGSL backends. Spelling the fused-
// multiply-add explicitly makes the intent compile-portable.
//
// Detection (purely AST):
//   1. call_expression to `lerp` with exactly 3 arguments;
//   2. first and second arguments are number_literal nodes (or unary minus
//      on a number_literal -- handled via text inspection).
//
// The fix is machine-applicable: `lerp(K1, K2, t) -> mad(t, K2 - K1, K1)`
// uses `t` exactly once (same as the original lerp), and `K1` / `K2` are
// numeric literals so repeating them is free. No side-effect duplication.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "select-vs-lerp-of-constant";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_lerp_name = "lerp";

/// True if `node` is a number_literal, OR a unary_expression `-N` where N is
/// a number_literal.
[[nodiscard]] bool is_numeric_literal(::TSNode node, std::string_view bytes) noexcept {
    const auto kind = node_kind(node);
    if (kind == "number_literal")
        return true;
    if (kind == "unary_expression") {
        // The operator must be `-` (or `+`); operand must be a number_literal.
        std::string_view op;
        ::TSNode operand{};
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            ::TSNode child = ::ts_node_child(node, i);
            if (::ts_node_is_null(child))
                continue;
            if (!::ts_node_is_named(child)) {
                if (op.empty())
                    op = node_text(child, bytes);
            } else if (::ts_node_is_null(operand)) {
                operand = child;
            }
        }
        if (::ts_node_is_null(operand)) {
            operand = ::ts_node_child_by_field_name(node, "argument", 8);
        }
        if (op != "-" && op != "+")
            return false;
        return node_kind(operand) == "number_literal";
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
        if (node_text(fn, bytes) == k_lerp_name) {
            const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 3U) {
                const ::TSNode k1 = ::ts_node_named_child(args, 0);
                const ::TSNode k2 = ::ts_node_named_child(args, 1);
                const ::TSNode t = ::ts_node_named_child(args, 2);

                if (is_numeric_literal(k1, bytes) && is_numeric_literal(k2, bytes)) {
                    const auto k1_text = node_text(k1, bytes);
                    const auto k2_text = node_text(k2, bytes);
                    const auto t_text = node_text(t, bytes);
                    if (!k1_text.empty() && !k2_text.empty() && !t_text.empty()) {
                        const auto call_range = tree.byte_range(node);

                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                        diag.message = std::string{
                            "`lerp(K1, K2, t)` with two constant endpoints relies on "
                            "the compiler's constant fold to collapse to a single mad "
                            "-- not all backends do this consistently; spelling "
                            "`mad(t, K2-K1, K1)` makes the intent portable"};

                        Fix fix;
                        // K1, K2 are numeric literals (no side effects to repeat)
                        // and `t` appears once on each side of the rewrite, so the
                        // mechanical replacement preserves observable behaviour.
                        fix.machine_applicable = true;
                        std::string replacement = std::string{"mad("} + std::string{t_text} + ", " +
                                                  std::string{k2_text} + " - " +
                                                  std::string{k1_text} + ", " +
                                                  std::string{k1_text} + ")";
                        fix.description = std::string{"replace with `"} + replacement +
                                          "` to make the multiply-add lowering compile-portable";
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                        edit.replacement = replacement;
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

class SelectVsLerpOfConstant : public Rule {
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

std::unique_ptr<Rule> make_select_vs_lerp_of_constant() {
    return std::make_unique<SelectVsLerpOfConstant>();
}

}  // namespace shader_clippy::rules
