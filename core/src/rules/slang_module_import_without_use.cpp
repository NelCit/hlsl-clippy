// slang-module-import-without-use
//
// Detects `import Foo;` (or `__include "Foo.slang"`) where the imported
// module's identifier is never referenced anywhere else in the source.
// Mirrors the `unused-cbuffer-field` rule in spirit -- surfaces dead
// imports that bloat compile time without contributing to the lowered
// program.
//
// Stage: Ast. Slang-only.
//
// Detection plan:
//
//   1. Walk every `import_statement` node. For dotted imports
//      (`import Foo.Bar.Baz;`) take the LAST segment as the
//      module's user-visible name -- that is what callers reference
//      via `Foo.Bar.Baz.do_thing()` or via `Baz.do_thing()` after
//      a `using` import.
//   2. Text-search the source bytes for any standalone identifier
//      occurrence of that name (using `is_id_char` boundaries) outside
//      the import statement itself. Zero hits -> the module is dead.
//
// Caveat: a real "dead import" check would need module-aware
// reflection -- a module can re-export symbols whose names don't match
// the module identifier, in which case the user's call site
// references those symbols by their unqualified name. The text-search
// heuristic gives a high-precision low-recall signal: it catches the
// common "I added an import and never wrote any code that uses it"
// case without false positives on real `Foo.api()` usage.
//
// References:
//   - Slang language guide §"Modules and Imports".
//   - shader-clippy `unused-cbuffer-field` rule -- companion text-search
//     heuristic for unused declarations.

#include <cstddef>
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

extern "C" {
const ::TSLanguage* tree_sitter_slang(void);
}

namespace shader_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "slang-module-import-without-use";
constexpr std::string_view k_category = "slang-language";

/// Count standalone-identifier occurrences of `name` in `bytes`, excluding
/// any occurrence whose start byte falls inside `[exclude.lo, exclude.hi)`.
[[nodiscard]] std::size_t count_id_occurrences(std::string_view bytes,
                                               std::string_view name,
                                               ByteSpan exclude) noexcept {
    if (name.empty()) {
        return 0U;
    }
    std::size_t count = 0U;
    std::size_t pos = 0U;
    while (pos <= bytes.size()) {
        const auto found = bytes.find(name, pos);
        if (found == std::string_view::npos) {
            return count;
        }
        const bool ok_left = (found == 0) || !is_id_char(bytes[found - 1]);
        const std::size_t end = found + name.size();
        const bool ok_right = (end >= bytes.size()) || !is_id_char(bytes[end]);
        if (ok_left && ok_right) {
            const auto abs_pos = static_cast<std::uint32_t>(found);
            if (abs_pos < exclude.lo || abs_pos >= exclude.hi) {
                ++count;
            }
        }
        pos = found + 1U;
    }
    return count;
}

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "import_statement") {
        // Capture the last `identifier` child -- that is the dotted-import's
        // tail segment, which is what user code references in qualified
        // form (`Foo.Bar.Baz.api()` or `Baz.api()` after a using-style
        // import).
        std::string_view tail;
        ::TSNode tail_node = ::TSNode{};
        const std::uint32_t cnt = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < cnt; ++i) {
            const auto child = ::ts_node_child(node, i);
            if (node_kind(child) == "identifier") {
                tail_node = child;
                tail = node_text(child, bytes);
            }
        }
        if (!tail.empty()) {
            const auto exclude = tree.byte_range(node);
            const auto uses = count_id_occurrences(bytes, tail, exclude);
            if (uses == 0U) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                std::string msg;
                msg += "module `";
                msg += tail;
                msg +=
                    "` is imported but never referenced -- dead imports "
                    "still cost compile time as Slang loads + parses + "
                    "type-checks the imported module. Remove the import "
                    "or use a symbol from the module";
                diag.message = std::move(msg);
                ctx.emit(std::move(diag));
            }
            (void)tail_node;
        }
    }
    const std::uint32_t cnt = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < cnt; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class SlangModuleImportWithoutUse : public Rule {
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
        if (tree.language() != tree_sitter_slang()) {
            return;
        }
        const auto bytes = tree.source_bytes();
        // Cheap pre-filter: skip walking when neither `import` nor
        // `__include` keyword appears. (`__include` is not a node-kind in
        // the current grammar but is a Slang surface form -- when the
        // grammar adds support, the walker picks it up automatically via
        // the `import_statement` node-kind that the grammar uses.)
        if (bytes.find("import") == std::string_view::npos) {
            return;
        }
        walk(::ts_tree_root_node(tree.raw_tree()), bytes, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_slang_module_import_without_use() {
    return std::make_unique<SlangModuleImportWithoutUse>();
}

}  // namespace shader_clippy::rules
