// redundant-unorm-snorm-conversion
//
// Detects an explicit `* (1.0/255.0)` (or `/ 255.0`) applied to an expression
// that looks like the result of a UNORM texture sample or load. Sampling a
// `R8G8B8A8_UNORM` (or any UNORM-format) texture already returns values in
// `[0, 1]`; the explicit divide is dead arithmetic on every IHV.
//
// Detection (purely AST, heuristic):
//
//   binary_expression
//     left:  any expression
//     right: number_literal `255.0` / `255.0f` / `255`
//     op:    `/`
//
//   OR
//
//   binary_expression
//     left:  any expression
//     right: parenthesized_expression containing `1.0 / 255.0` (literal/literal)
//     op:    `*`
//
// The Phase 3 follow-up (per ADR 0011) tightens this with reflection so it
// only fires when the source is provably UNORM. For Phase 2 we ship the
// AST-only heuristic; the fix is suggestion-only because the literal `255`
// is occasionally used to convert a typed-`uint` channel value (e.g. from a
// `Buffer<uint>`) into the [0,1] range -- a case the heuristic cannot
// distinguish from a UNORM-sample double-divide.

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

constexpr std::string_view k_rule_id = "redundant-unorm-snorm-conversion";
constexpr std::string_view k_category = "math";

[[nodiscard]] bool is_float_suffix(char c) noexcept {
    return c == 'f' || c == 'F' || c == 'h' || c == 'H' || c == 'l' || c == 'L';
}

/// True if `text` is a numeric literal whose value is exactly 255 (with
/// optional `.0...0`, optional float suffix).
[[nodiscard]] bool literal_is_255(std::string_view text) noexcept {
    if (text.empty())
        return false;
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+')
        ++i;
    while (i < text.size() && text[i] == '0')
        ++i;
    // Now expect the digits "255".
    if (i + 3 > text.size())
        return false;
    if (text[i] != '2' || text[i + 1] != '5' || text[i + 2] != '5')
        return false;
    i += 3;
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

/// True if `node` is a parenthesized expression of the form `(1.0 / 255.0)`
/// (i.e. a binary_expression `/` with left = literal 1, right = literal 255).
[[nodiscard]] bool is_one_over_255(::TSNode node, std::string_view bytes) noexcept {
    // Strip parens.
    while (node_kind(node) == "parenthesized_expression" &&
           ::ts_node_named_child_count(node) >= 1U) {
        node = ::ts_node_named_child(node, 0);
    }
    if (node_kind(node) != "binary_expression")
        return false;
    if (binary_op(node, bytes) != "/")
        return false;
    const ::TSNode l = ::ts_node_child_by_field_name(node, "left", 4);
    const ::TSNode r = ::ts_node_child_by_field_name(node, "right", 5);
    if (node_kind(l) != "number_literal" || node_kind(r) != "number_literal")
        return false;
    return literal_is_one(node_text(l, bytes)) && literal_is_255(node_text(r, bytes));
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "binary_expression") {
        const auto op = binary_op(node, bytes);
        const ::TSNode r = ::ts_node_child_by_field_name(node, "right", 5);

        bool fires = false;
        if (op == "/" && node_kind(r) == "number_literal" && literal_is_255(node_text(r, bytes))) {
            fires = true;
        } else if (op == "*" && is_one_over_255(r, bytes)) {
            fires = true;
        }

        if (fires) {
            const auto expr_range = tree.byte_range(node);
            const ::TSNode l = ::ts_node_child_by_field_name(node, "left", 4);
            const auto left_text = node_text(l, bytes);

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(), .bytes = expr_range};
            diag.message = std::string{
                "explicit `/ 255.0` (or `* (1.0/255.0)`) on a sampled value is "
                "almost always a leftover UNORM conversion -- UNORM textures "
                "already return values in [0, 1] from `Sample` / `Load`"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description =
                std::string{
                    "if the source is a UNORM texture, drop the divide "
                    "(replace the expression with `"} +
                std::string{left_text} +
                "`); confirm the source format before "
                "applying";
            if (!left_text.empty()) {
                TextEdit edit;
                edit.span = Span{.source = tree.source_id(), .bytes = expr_range};
                edit.replacement = std::string{left_text};
                fix.edits.push_back(std::move(edit));
            }
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
            // Don't double-fire on the same expression's nested children.
            return;
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class RedundantUnormSnormConversion : public Rule {
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

std::unique_ptr<Rule> make_redundant_unorm_snorm_conversion() {
    return std::make_unique<RedundantUnormSnormConversion>();
}

}  // namespace hlsl_clippy::rules
