// sqrt-of-potentially-negative
//
// Detects `sqrt(x)` where `x` is a `dot(a, a)`-style expression that the
// rounding could push slightly below zero (a known FP hazard producing NaN),
// or any expression containing a subtraction at the top level (likely
// difference-of-squares pattern). The fix is to wrap the argument in
// `max(0.0, ...)`.
//
// Stage: Ast. Textual detection on `call_expression` whose function is
// `sqrt`.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

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

constexpr std::string_view k_rule_id = "sqrt-of-potentially-negative";
constexpr std::string_view k_category = "math";

/// True if `arg` is a textual pattern that can round below zero. We flag two
/// patterns for now: any `... - ...` at the top level (difference-of-squares),
/// and any `dot(<x>, <x>)` minus something. Patterns that are trivially
/// non-negative (e.g. `x * x`, single literal) are skipped.
[[nodiscard]] bool can_round_negative(std::string_view arg) noexcept {
    // Detect a subtraction at depth-0 paren level.
    int depth = 0;
    bool prev_was_op = true;  // start-of-expr counts as "expecting a primary"
    for (std::size_t i = 0; i < arg.size(); ++i) {
        const char c = arg[i];
        if (c == '(') {
            ++depth;
            prev_was_op = true;
            continue;
        }
        if (c == ')') {
            if (depth > 0)
                --depth;
            prev_was_op = false;
            continue;
        }
        if (depth == 0 && c == '-' && !prev_was_op) {
            return true;
        }
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            prev_was_op = (c == '+' || c == '-' || c == '*' || c == '/' || c == ',' || c == '<' ||
                           c == '>' || c == '=' || c == '!' || c == '&' || c == '|');
        }
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "call_expression") {
        const auto fn = ::ts_node_child_by_field_name(node, "function", 8);
        const auto fn_text = node_text(fn, bytes);
        if (fn_text == "sqrt") {
            const auto args = ::ts_node_child_by_field_name(node, "arguments", 9);
            if (!::ts_node_is_null(args) && ::ts_node_named_child_count(args) >= 1U) {
                const auto arg0 = ::ts_node_named_child(args, 0);
                const auto arg_text = node_text(arg0, bytes);
                // Skip when the argument is already guarded by max(0, ...).
                std::string_view trimmed = arg_text;
                while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
                    trimmed.remove_prefix(1);
                }
                if (trimmed.starts_with("max(") || trimmed.starts_with("max (")) {
                    // Already guarded.
                } else if (can_round_negative(arg_text)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{
                        "sqrt() of a difference expression may round negative, "
                        "producing NaN; wrap the argument in max(0.0, ...) (or "
                        "use rsqrt + reciprocal for the unit-vector case)"};

                    Fix fix;
                    fix.machine_applicable = false;
                    fix.description = std::string{
                        "wrap the argument in max(0.0, ...) -- the clamp is free "
                        "on RDNA/Ada (compiles to a SAT-style modifier on the "
                        "producing instruction) and prevents NaN propagation"};
                    diag.fixes.push_back(std::move(fix));

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

class SqrtOfPotentiallyNegative : public Rule {
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

std::unique_ptr<Rule> make_sqrt_of_potentially_negative() {
    return std::make_unique<SqrtOfPotentiallyNegative>();
}

}  // namespace hlsl_clippy::rules
