// length-then-divide
//
// Detects `v / length(v)` (where the inner argument to `length` is textually
// identical to the dividend) and suggests `normalize(v)`. `normalize` is the
// named idiom for unit-vector construction and may compile to a more
// efficient sequence (one rsqrt-and-multiply) than the literal divide.
//
// The fix is machine-applicable when the dividend `v` is a simple identifier;
// for more complex expressions we still emit the rewrite (since `normalize`
// only takes one argument copy) but the user should verify the dividend is
// side-effect-free.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "length-then-divide";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_length_name = "length";

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

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression" && binary_op(node, bytes) == "/") {
        const ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        const ::TSNode rhs = ::ts_node_child_by_field_name(node, "right", 5);
        if (!::ts_node_is_null(lhs) && node_kind(rhs) == "call_expression") {
            const ::TSNode fn = ::ts_node_child_by_field_name(rhs, "function", 8);
            if (node_text(fn, bytes) == k_length_name) {
                const ::TSNode args = ::ts_node_child_by_field_name(rhs, "arguments", 9);
                if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) == 1U) {
                    const ::TSNode inner = ::ts_node_named_child(args, 0);
                    const auto v_text = node_text(lhs, bytes);
                    const auto inner_text = node_text(inner, bytes);
                    if (!v_text.empty() && v_text == inner_text) {
                        const auto expr_range = tree.byte_range(node);
                        const bool is_simple = (node_kind(lhs) == "identifier");

                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
                        diag.message = std::string{
                            "`v / length(v)` is the open-coded form of "
                            "`normalize(v)` -- prefer the named intrinsic, which "
                            "may compile to a single rsqrt-and-multiply"};

                        Fix fix;
                        fix.machine_applicable = is_simple;
                        fix.description =
                            is_simple ? std::string{"replace `v / length(v)` with `normalize(v)`"}
                                      : std::string{
                                            "replace with `normalize(v)`; verify the "
                                            "dividend is side-effect-free before applying"};
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
                        std::string replacement;
                        replacement.reserve(v_text.size() + 12);
                        replacement.append("normalize(");
                        replacement.append(v_text);
                        replacement.append(")");
                        edit.replacement = std::move(replacement);
                        fix.edits.push_back(std::move(edit));
                        diag.fixes.push_back(std::move(fix));

                        ctx.emit(std::move(diag));
                        // Don't descend -- prevents emitting another diagnostic for
                        // the inner length call from a different rule's perspective
                        // (actually, descent is harmless; we just avoid re-walking).
                        return;
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

class LengthThenDivide : public Rule {
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

std::unique_ptr<Rule> make_length_then_divide() {
    return std::make_unique<LengthThenDivide>();
}

}  // namespace shader_clippy::rules
