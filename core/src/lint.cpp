#include "hlsl_clippy/lint.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"

namespace hlsl_clippy {

namespace {

// RAII wrapper for TSTreeCursor; the API uses by-value initialise/destroy
// pairs so a unique_ptr is overkill, but we still want exception safety.
class TreeCursor {
public:
    explicit TreeCursor(::TSNode root) noexcept : cursor_(ts_tree_cursor_new(root)) {}
    TreeCursor(const TreeCursor&) = delete;
    TreeCursor& operator=(const TreeCursor&) = delete;
    TreeCursor(TreeCursor&&) = delete;
    TreeCursor& operator=(TreeCursor&&) = delete;
    ~TreeCursor() {
        ts_tree_cursor_delete(&cursor_);
    }

    [[nodiscard]] ::TSTreeCursor* raw() noexcept {
        return &cursor_;
    }

private:
    ::TSTreeCursor cursor_;
};

/// Iterative depth-first walk of the tree-sitter CST. Calls each rule's
/// `on_node` for every named node in document order.
void walk(::TSNode root,
          std::span<const std::unique_ptr<Rule>> rules,
          RuleContext& ctx,
          std::string_view source_bytes,
          SourceId source_id) {
    if (ts_node_is_null(root)) {
        return;
    }
    TreeCursor cursor{root};

    while (true) {
        const ::TSNode node = ts_tree_cursor_current_node(cursor.raw());
        if (ts_node_is_named(node)) {
            const AstCursor ast{node, source_bytes, source_id};
            for (const auto& rule : rules) {
                rule->on_node(ast, ctx);
            }
        }

        // Descend, then move sideways, then climb until we find a sibling or
        // exhaust the tree.
        if (ts_tree_cursor_goto_first_child(cursor.raw())) {
            continue;
        }
        while (!ts_tree_cursor_goto_next_sibling(cursor.raw())) {
            if (!ts_tree_cursor_goto_parent(cursor.raw())) {
                return;  // Walked back past the root; done.
            }
        }
    }
}

}  // namespace

std::vector<Diagnostic> lint(const SourceManager& sources,
                             SourceId source,
                             std::span<const std::unique_ptr<Rule>> rules) {
    auto parsed = parser::parse(sources, source);
    if (!parsed) {
        return {};
    }

    RuleContext ctx{sources, source};
    const ::TSNode root = ts_tree_root_node(parsed->tree.get());
    walk(root, rules, ctx, parsed->bytes, parsed->source);
    return ctx.take_diagnostics();
}

}  // namespace hlsl_clippy
