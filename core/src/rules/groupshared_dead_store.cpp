// groupshared-dead-store
//
// Detects writes to a `groupshared` cell whose value is never read on any
// CFG path to function exit -- neither by the writing thread nor by any
// other thread.
//
// Stage: `ControlFlow`. The rule pairs an AST scan that locates groupshared
// declarations and write-form `subscript_expression`s with a per-write
// `light_dataflow::dead_store` query against the CFG.
//
// Forward-compatible-stub status: per `core/src/rules/util/light_dataflow.hpp`,
// `dead_store` returns `false` until sub-phase 4a's CFG grows per-variable
// use-def chains. Until then this rule emits zero diagnostics by design --
// the API shape is locked so the rule compiles, registers, and ships against
// the engine; precision tightens as the engine grows.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/light_dataflow.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-dead-store";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] bool is_id_boundary(char c) noexcept {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
             c == '_');
}

[[nodiscard]] bool has_keyword(std::string_view text, std::string_view keyword) noexcept {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto found = text.find(keyword, pos);
        if (found == std::string_view::npos) {
            return false;
        }
        const bool ok_left = (found == 0U) || is_id_boundary(text[found - 1U]);
        const std::size_t end = found + keyword.size();
        const bool ok_right = (end >= text.size()) || is_id_boundary(text[end]);
        if (ok_left && ok_right) {
            return true;
        }
        pos = found + 1U;
    }
    return false;
}

void collect_groupshared_decls(::TSNode node,
                               std::string_view bytes,
                               std::unordered_set<std::string>& out_names) {
    if (::ts_node_is_null(node)) {
        return;
    }
    const auto kind = node_kind(node);
    const bool decl_like =
        (kind == "declaration" || kind == "field_declaration" ||
         kind == "global_variable_declaration" || kind == "variable_declaration");
    if (decl_like) {
        const auto text = node_text(node, bytes);
        if (has_keyword(text, "groupshared")) {
            const ::TSNode declarator = ::ts_node_child_by_field_name(node, "declarator", 10);
            if (!::ts_node_is_null(declarator)) {
                ::TSNode name_node = declarator;
                if (node_kind(declarator) == "init_declarator" ||
                    node_kind(declarator) == "array_declarator") {
                    const ::TSNode inner =
                        ::ts_node_child_by_field_name(declarator, "declarator", 10);
                    if (!::ts_node_is_null(inner)) {
                        name_node = inner;
                    }
                }
                if (node_kind(name_node) == "array_declarator") {
                    const ::TSNode inner =
                        ::ts_node_child_by_field_name(name_node, "declarator", 10);
                    if (!::ts_node_is_null(inner)) {
                        name_node = inner;
                    }
                }
                const auto name = node_text(name_node, bytes);
                if (!name.empty()) {
                    out_names.insert(std::string{name});
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_groupshared_decls(::ts_node_child(node, i), bytes, out_names);
    }
}

/// Walk to find every `assignment_expression` whose left-hand side is a
/// `subscript_expression` whose receiver is one of the recorded groupshared
/// names. For each such write we ask `light_dataflow::dead_store` whether
/// the underlying cell has any subsequent reachable read; if not, we emit.
void scan_writes(::TSNode node,
                 std::string_view bytes,
                 const std::unordered_set<std::string>& names,
                 const ControlFlowInfo& cfg,
                 const AstTree& tree,
                 RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "assignment_expression") {
        const ::TSNode lhs = ::ts_node_child_by_field_name(node, "left", 4);
        if (!::ts_node_is_null(lhs) && node_kind(lhs) == "subscript_expression") {
            ::TSNode receiver = ::ts_node_child_by_field_name(lhs, "argument", 8);
            if (::ts_node_is_null(receiver)) {
                receiver = ::ts_node_child(lhs, 0U);
            }
            const auto receiver_text = node_text(receiver, bytes);
            if (!receiver_text.empty() && names.contains(std::string{receiver_text})) {
                const Span lhs_span{
                    .source = tree.source_id(),
                    .bytes = tree.byte_range(lhs),
                };
                if (util::dead_store(cfg, lhs_span)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{"write to groupshared cell `"} +
                                   std::string{receiver_text} +
                                   "` is dead -- no reachable read on any CFG path; remove "
                                   "the store (and possibly the LDS allocation) to recover "
                                   "LDS bandwidth and occupancy budget";
                    ctx.emit(std::move(diag));
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_writes(::ts_node_child(node, i), bytes, names, cfg, tree, ctx);
    }
}

class GroupsharedDeadStore : public Rule {
public:
    [[nodiscard]] std::string_view id() const noexcept override {
        return k_rule_id;
    }
    [[nodiscard]] std::string_view category() const noexcept override {
        return k_category;
    }
    [[nodiscard]] Stage stage() const noexcept override {
        return Stage::ControlFlow;
    }

    void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx) override {
        const ::TSNode root = ::ts_tree_root_node(tree.raw_tree());
        const auto bytes = tree.source_bytes();

        std::unordered_set<std::string> names;
        collect_groupshared_decls(root, bytes, names);
        if (names.empty()) {
            return;
        }
        scan_writes(root, bytes, names, cfg, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_dead_store() {
    return std::make_unique<GroupsharedDeadStore>();
}

}  // namespace hlsl_clippy::rules
