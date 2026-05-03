// groupshared-volatile
//
// Detects a `volatile` qualifier on a `groupshared` declaration. Under the
// HLSL memory model, ordering between threads in the same workgroup is
// established by `GroupMemoryBarrier*` calls; ordering across UAV writes is
// established by `globallycoherent`. `volatile` does neither: it confuses the
// optimiser into pessimising LDS scheduling on every IHV without buying any
// observable ordering guarantee.
//
// Detection is purely textual on a top-level declaration node: we walk the
// tree looking for any node whose source text begins with both `groupshared`
// and `volatile` keywords (in either order, possibly separated by whitespace
// and other qualifiers). The fix is machine-applicable: drop the `volatile`
// token.

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

constexpr std::string_view k_rule_id = "groupshared-volatile";
constexpr std::string_view k_category = "workgroup";

/// True if `c` is whitespace or a punctuation char that ends an identifier
/// (so that we can confirm `groupshared` / `volatile` were full keywords and
/// not the prefix of a longer identifier).
[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

/// Find the byte offset of `keyword` in `text` if it appears as a complete
/// token (preceded by start-of-string or a non-identifier char, followed by
/// a non-identifier char). Returns `std::string_view::npos` on miss.
[[nodiscard]] std::size_t find_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos)
            return std::string_view::npos;
        const bool ok_left = (found == 0) || is_id_boundary(text[found - 1]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right)
            return found;
        pos = found + 1;
    }
    return std::string_view::npos;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    const auto kind = node_kind(node);
    // Match any declaration-like node. The grammar's exact name for the
    // variable-declaration node varies; accept anything that looks like a
    // declaration. We deliberately skip `translation_unit` here so that we
    // descend to the actual declaration node and avoid one fire per file.
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto text = node_text(node, bytes);
        const auto gs_pos = find_keyword(text, "groupshared");
        const auto vol_pos = find_keyword(text, "volatile");
        if (gs_pos != std::string_view::npos && vol_pos != std::string_view::npos) {
            // Compute the absolute byte offset of the `volatile` token.
            const auto node_lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
            const std::uint32_t vol_abs_lo = node_lo + static_cast<std::uint32_t>(vol_pos);
            const std::uint32_t vol_abs_hi =
                vol_abs_lo + static_cast<std::uint32_t>(std::string_view{"volatile"}.size());

            // Try to also strip a trailing whitespace char so the fix
            // doesn't leave a double space. Look at the byte after.
            std::uint32_t end_strip = vol_abs_hi;
            if (end_strip < bytes.size() && (bytes[end_strip] == ' ' || bytes[end_strip] == '\t')) {
                ++end_strip;
            }

            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = Span{.source = tree.source_id(),
                                     .bytes = ByteSpan{.lo = vol_abs_lo, .hi = vol_abs_hi}};
            diag.message = std::string{
                "`volatile` on a `groupshared` declaration is meaningless under "
                "the HLSL memory model -- use `GroupMemoryBarrier*` for LDS "
                "ordering, or `globallycoherent` for cross-CU UAV ordering"};

            Fix fix;
            fix.machine_applicable = true;
            fix.description = std::string{
                "drop the `volatile` qualifier (it does not affect LDS "
                "ordering and pessimises the optimiser)"};
            TextEdit edit;
            edit.span = Span{.source = tree.source_id(),
                             .bytes = ByteSpan{.lo = vol_abs_lo, .hi = end_strip}};
            edit.replacement = std::string{};
            fix.edits.push_back(std::move(edit));
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
            // Don't descend into matched declaration: avoids re-firing on
            // child init_declarator / qualifier subnodes that share the text.
            return;
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class GroupsharedVolatile : public Rule {
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

std::unique_ptr<Rule> make_groupshared_volatile() {
    return std::make_unique<GroupsharedVolatile>();
}

}  // namespace shader_clippy::rules
