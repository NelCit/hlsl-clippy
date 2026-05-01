// firstbit-vs-log2-trick
//
// Detects `(uint)log2(x)` -- the textbook hack for "find the index of the
// highest set bit". HLSL ships `firstbithigh(uint)` which lowers to a single
// hardware findMSB instruction on every modern GPU; the log2 trick eats a
// transcendental + an int<->float round-trip for the same answer (with worse
// precision near power-of-two boundaries to boot).
//
// We match a `cast_expression` whose target type identifier is `uint` (or
// `uint`-prefixed vector type) and whose operand is a `call_expression` to
// `log2`. The operand inside `log2(...)` may itself be an `(float)x` cast --
// the trick almost always pays the round-trip explicitly.
//
// The fix is SUGGESTION-ONLY: `firstbithigh` returns the bit index, while
// `(uint)log2(0)` returns garbage / -inf-cast; the developer needs to confirm
// that the input is non-zero or wants the firstbithigh sentinel behaviour.

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

constexpr std::string_view k_rule_id = "firstbit-vs-log2-trick";
constexpr std::string_view k_category = "math";

/// True if `text` names a uint-family scalar or vector type: `uint`, `uint2`,
/// `uint3`, `uint4`. We deliberately exclude `int`/`float`-cast lookalikes.
[[nodiscard]] bool is_uint_type_name(std::string_view text) noexcept {
    if (text == "uint")
        return true;
    if (text.size() == 5 && text.starts_with("uint") &&
        (text[4] == '2' || text[4] == '3' || text[4] == '4')) {
        return true;
    }
    return false;
}

/// True if `node` is a `call_expression` to `log2`, with one argument; if so,
/// the argument node is returned via `inner_arg_out`.
[[nodiscard]] bool is_log2_call(::TSNode node,
                                std::string_view bytes,
                                ::TSNode& inner_arg_out) noexcept {
    if (node_kind(node) != "call_expression")
        return false;
    const ::TSNode fn = ::ts_node_child_by_field_name(node, "function", 8);
    if (node_text(fn, bytes) != "log2")
        return false;
    const ::TSNode args = ::ts_node_child_by_field_name(node, "arguments", 9);
    if (::ts_node_is_null(args) || ::ts_node_named_child_count(args) != 1U)
        return false;
    inner_arg_out = ::ts_node_named_child(args, 0);
    return !::ts_node_is_null(inner_arg_out);
}

/// Recursively peel off any `(float)` / `(float3)` / `parenthesized_expression`
/// wrappers and return the deepest underlying expression text. Used to suggest
/// the original integer operand spelling for `firstbithigh(...)`.
[[nodiscard]] std::string_view peel_to_inner(::TSNode node, std::string_view bytes) noexcept {
    for (;;) {
        const auto kind = node_kind(node);
        if (kind == "parenthesized_expression") {
            if (::ts_node_named_child_count(node) < 1U)
                break;
            node = ::ts_node_named_child(node, 0);
            continue;
        }
        if (kind == "cast_expression") {
            // Only peel float-family casts; otherwise stop here.
            const ::TSNode tt = ::ts_node_child_by_field_name(node, "type", 4);
            const auto type_text = node_text(tt, bytes);
            const bool is_float_cast = type_text == "float" || type_text == "float2" ||
                                       type_text == "float3" || type_text == "float4" ||
                                       type_text == "double" || type_text == "half";
            if (!is_float_cast)
                break;
            const ::TSNode val = ::ts_node_child_by_field_name(node, "value", 5);
            if (::ts_node_is_null(val))
                break;
            node = val;
            continue;
        }
        break;
    }
    return node_text(node, bytes);
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "cast_expression") {
        const ::TSNode tt = ::ts_node_child_by_field_name(node, "type", 4);
        const auto type_text = node_text(tt, bytes);
        if (is_uint_type_name(type_text)) {
            const ::TSNode val = ::ts_node_child_by_field_name(node, "value", 5);
            ::TSNode probe = val;
            // Strip parentheses around the log2 call.
            while (!::ts_node_is_null(probe) && node_kind(probe) == "parenthesized_expression" &&
                   ::ts_node_named_child_count(probe) >= 1U) {
                probe = ::ts_node_named_child(probe, 0);
            }
            ::TSNode inner_arg{};
            if (is_log2_call(probe, bytes, inner_arg)) {
                const auto cast_range = tree.byte_range(node);
                const auto inner_text = peel_to_inner(inner_arg, bytes);

                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = Span{.source = tree.source_id(), .bytes = cast_range};
                diag.message = std::string{
                    "`(uint)log2(x)` open-codes the highest-set-bit index via a "
                    "transcendental + integer cast -- use `firstbithigh(x)`, which "
                    "lowers to a single hardware findMSB instruction"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description =
                    inner_text.empty()
                        ? std::string{"replace `(uint)log2(x)` with `firstbithigh(x)`; "
                                      "verify that x is non-zero or that you want the "
                                      "firstbithigh sentinel for zero"}
                        : std::string{"replace with `firstbithigh("} + std::string{inner_text} +
                              ")`; verify that the operand is non-zero or that you want the "
                              "firstbithigh sentinel value for zero";
                if (!inner_text.empty()) {
                    TextEdit edit;
                    edit.span = Span{.source = tree.source_id(), .bytes = cast_range};
                    edit.replacement = std::string{"firstbithigh("} + std::string{inner_text} + ")";
                    fix.edits.push_back(std::move(edit));
                }
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
                return;  // Don't double-fire on nested casts inside the same expr.
            }
        }
    }

    const uint32_t count = ::ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class FirstBitVsLog2Trick : public Rule {
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

std::unique_ptr<Rule> make_firstbit_vs_log2_trick() {
    return std::make_unique<FirstBitVsLog2Trick>();
}

}  // namespace hlsl_clippy::rules
