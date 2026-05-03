// lerp-on-bool-cond
//
// Detects `lerp(a, b, (float)cond)` and `lerp(a, b, cond ? 1.0 : 0.0)` where
// the t-argument is a cast or select on a boolean. The portable spelling is
// `cond ? b : a` (or `select(cond, b, a)`): on RDNA / Ada the bool->float
// cast usually folds to `select`, but on Xe-HPG it has been observed to
// lower to a real `lerp` (one mul + one mad) which is wasted ALU.
//
// Detection (purely AST-textual):
//   1. call_expression to `lerp` with exactly 3 arguments;
//   2. third argument is either:
//      a. a cast_expression whose target type is float-family
//         (float/float2/float3/float4/half), OR
//      b. a conditional_expression whose `consequence` and `alternative` are
//         the literals 1 and 0 (or 0 and 1).
//
// Fix grade (v1.2 — ADR 0019, side-effect-purity oracle):
//   * machine-applicable when both `a` and `b` are SideEffectFree per
//     `purity_oracle::classify_expression`. The rewrite duplicates one of
//     `{a, b}` along the false branch of the resulting `?:` and dropping the
//     other along the true branch -- safe iff neither has observable side
//     effects.
//   * suggestion-only otherwise (a non-pure operand would be evaluated twice
//     under the rewrite, OR not at all -- an observable change). Phase-1
//     baseline behaviour for the rare impure-arg case.

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
#include "rules/util/purity_oracle.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "lerp-on-bool-cond";
constexpr std::string_view k_category = "math";
constexpr std::string_view k_lerp_name = "lerp";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

[[nodiscard]] bool literal_is_zero(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
        return false;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i < text.size() && text[i] >= '1' && text[i] <= '9')
        return false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        if (!is_float_suffix(text[i]))
            return false;
        ++i;
    }
    return true;
}

[[nodiscard]] bool literal_is_one(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    while (i < text.size() && text[i] == '0')
        ++i;
    if (i >= text.size() || text[i] != '1')
        return false;
    ++i;
    if (i < text.size() && text[i] >= '0' && text[i] <= '9')
        return false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] == '0')
            ++i;
        if (i < text.size() && text[i] >= '1' && text[i] <= '9')
            return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        return false;
    while (i < text.size()) {
        if (!is_float_suffix(text[i]))
            return false;
        ++i;
    }
    return true;
}

[[nodiscard]] bool is_float_family_type(std::string_view name) noexcept {
    return name == "float" || name == "float2" || name == "float3" || name == "float4" ||
           name == "half" || name == "half2" || name == "half3" || name == "half4" ||
           name == "min16float";
}

/// Examine the third lerp argument. Returns true if it matches one of the
/// patterns the rule fires on. On true:
///   - `cond_text_out` is set to the text of the boolean condition;
///   - `swap_out` is `false` for `(float)cond` and for `cond ? 1 : 0`,
///     and `true` for `cond ? 0 : 1` (so the rewrite is `cond ? a : b`).
[[nodiscard]] bool match_t_arg(::TSNode t,
                               std::string_view bytes,
                               std::string& cond_text_out,
                               bool& swap_out) noexcept {
    const auto kind = node_kind(t);
    if (kind == "cast_expression") {
        const ::TSNode tt = ::ts_node_child_by_field_name(t, "type", 4);
        if (!is_float_family_type(node_text(tt, bytes)))
            return false;
        const ::TSNode val = ::ts_node_child_by_field_name(t, "value", 5);
        if (::ts_node_is_null(val))
            return false;
        // Require the inner expression to be a plain identifier or
        // field_expression (e.g. `flags.x`); skip more complex forms to keep
        // the false-positive rate low.
        const auto vk = node_kind(val);
        if (vk != "identifier" && vk != "field_expression")
            return false;
        cond_text_out = std::string{node_text(val, bytes)};
        swap_out = false;
        return !cond_text_out.empty();
    }
    if (kind == "conditional_expression") {
        const ::TSNode cond = ::ts_node_child_by_field_name(t, "condition", 9);
        const ::TSNode then_n = ::ts_node_child_by_field_name(t, "consequence", 11);
        const ::TSNode else_n = ::ts_node_child_by_field_name(t, "alternative", 11);
        if (::ts_node_is_null(cond) || ::ts_node_is_null(then_n) || ::ts_node_is_null(else_n))
            return false;
        if (node_kind(then_n) != "number_literal" || node_kind(else_n) != "number_literal")
            return false;
        const auto then_text = node_text(then_n, bytes);
        const auto else_text = node_text(else_n, bytes);
        if (literal_is_one(then_text) && literal_is_zero(else_text)) {
            cond_text_out = std::string{node_text(cond, bytes)};
            swap_out = false;
            return !cond_text_out.empty();
        }
        if (literal_is_zero(then_text) && literal_is_one(else_text)) {
            cond_text_out = std::string{node_text(cond, bytes)};
            swap_out = true;
            return !cond_text_out.empty();
        }
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
                const ::TSNode a = ::ts_node_named_child(args, 0);
                const ::TSNode b = ::ts_node_named_child(args, 1);
                const ::TSNode t = ::ts_node_named_child(args, 2);

                std::string cond_text;
                bool swap = false;
                if (match_t_arg(t, bytes, cond_text, swap)) {
                    const auto a_text = node_text(a, bytes);
                    const auto b_text = node_text(b, bytes);
                    if (!a_text.empty() && !b_text.empty()) {
                        const auto call_range = tree.byte_range(node);
                        const std::string& consequence =
                            swap ? std::string{a_text} : std::string{b_text};
                        const std::string& alternative =
                            swap ? std::string{b_text} : std::string{a_text};

                        // The `?:` rewrite drops one of {a, b} along each
                        // branch -- safe to apply automatically iff neither
                        // operand has observable side effects (ADR 0019,
                        // purity oracle). If either operand is impure or
                        // unclassifiable, fall back to a suggestion-grade
                        // fix that the user must hand-review.
                        const auto a_purity = util::classify_expression(tree, a);
                        const auto b_purity = util::classify_expression(tree, b);
                        const bool both_pure = a_purity == util::Purity::SideEffectFree &&
                                               b_purity == util::Purity::SideEffectFree;

                        Diagnostic diag;
                        diag.code = std::string{k_rule_id};
                        diag.severity = Severity::Warning;
                        diag.primary_span = Span{.source = tree.source_id(), .bytes = call_range};
                        diag.message = std::string{
                            "`lerp(a, b, <bool>)` produces a portable-codegen mismatch "
                            "(some IHVs fold to select; others emit a real lerp) -- "
                            "use `cond ? b : a` (or `select(cond, b, a)`) for a "
                            "stable lowering"};

                        Fix fix;
                        fix.machine_applicable = both_pure;
                        fix.description =
                            std::string{"replace with `"} + cond_text + " ? " + consequence +
                            " : " + alternative + "` for portable codegen across IHVs" +
                            (both_pure ? std::string{}
                                       : std::string{" (suggestion-only: at least one of `a` / `b` "
                                                     "is not side-effect-free, applying the "
                                                     "rewrite would change the evaluation count)"});
                        TextEdit edit;
                        edit.span = Span{.source = tree.source_id(), .bytes = call_range};
                        edit.replacement = cond_text + " ? " + consequence + " : " + alternative;
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

class LerpOnBoolCond : public Rule {
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

std::unique_ptr<Rule> make_lerp_on_bool_cond() {
    return std::make_unique<LerpOnBoolCond>();
}

}  // namespace hlsl_clippy::rules
