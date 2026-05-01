// groupshared-union-aliased
//
// Detects a `groupshared` declaration whose underlying type aliases two or
// more typed views over the same byte offset. Two heuristics, both observed
// in the wild on real shaders:
//
//   1. The declaration's type subtree contains the keyword `union` -- HLSL
//      doesn't officially support unions in shader code, but driver compilers
//      have accepted them for years and authors use them for typed aliasing.
//   2. The declaration's type subtree contains a struct with at least one
//      bit-field declaration (`uint flags : 8;`). Bit-fields-on-LDS are the
//      classic struct-hack alternative to a union.
//
// In both cases the optimiser cannot reason about the LDS aliasing it
// implies and falls back to round-tripping every access through memory --
// each typed view forces a serialised read-modify-write across the wave.
//
// Detection (purely AST):
//   * Walk top-level declarations and field declarations, find ones whose
//     subtree text contains both `groupshared` (as a complete keyword) and
//     either `union` (as a complete keyword) or a bit-field-shaped field
//     (a `:` between an identifier and a number_literal inside a struct
//     body that is a child of the declaration).
//
// The fix is suggestion-only: the rewrite to a single-typed view + explicit
// helper functions changes the shader's API surface.

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

constexpr std::string_view k_rule_id = "groupshared-union-aliased";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// True iff `keyword` appears in `text` as a complete identifier (preceded /
/// followed by a non-identifier character or string boundary).
[[nodiscard]] bool has_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos)
            return false;
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right)
            return true;
        pos = found + 1U;
    }
    return false;
}

/// True iff `node`'s subtree contains a `field_declaration` whose declarator
/// has the bit-field shape `: <number_literal>`. We detect this by walking
/// the subtree for any `field_declaration` whose source text contains a
/// `:` followed by an integer literal (skipping over any whitespace).
[[nodiscard]] bool subtree_has_bitfield(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return false;
    if (node_kind(node) == "field_declaration") {
        const auto text = node_text(node, bytes);
        const auto colon = text.find(':');
        if (colon != std::string_view::npos) {
            std::size_t i = colon + 1U;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
                ++i;
            if (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                // Excludes single-character semantics (`: SV_Target`) since
                // the next char must be a digit.
                return true;
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (subtree_has_bitfield(::ts_node_child(node, i), bytes))
            return true;
    }
    return false;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    const auto kind = node_kind(node);
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto text = node_text(node, bytes);
        if (has_keyword(text, "groupshared")) {
            const bool has_union = has_keyword(text, "union");
            const bool has_bitfield = subtree_has_bitfield(node, bytes);
            if (has_union || has_bitfield) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "`groupshared` declaration aliases two typed views over the "
                    "same offset (union or bit-field struct hack) -- the "
                    "optimiser cannot reason about LDS aliasing and falls back "
                    "to round-tripping every access through memory"};
                ctx.emit(std::move(diag));
                // Avoid descending: skip the inner field_declaration nodes
                // whose subtree text would re-trigger the keyword match.
                return;
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class GroupsharedUnionAliased : public Rule {
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

std::unique_ptr<Rule> make_groupshared_union_aliased() {
    return std::make_unique<GroupsharedUnionAliased>();
}

}  // namespace hlsl_clippy::rules
