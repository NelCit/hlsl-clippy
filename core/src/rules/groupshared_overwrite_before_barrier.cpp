// groupshared-overwrite-before-barrier
//
// Detects two same-thread writes to the same `groupshared` cell with no
// `GroupMemoryBarrier*` (or `AllMemoryBarrier*`) intervening. The first
// write is unobservable to other threads because the second write
// supersedes it before any cross-thread synchronisation could expose the
// original value -- one wasted LDS write cycle per wave per execution.
//
// Stage: `ControlFlow`. The rule pairs an AST scan that pairs up two
// writes to the same groupshared cell on the same enclosing function with
// a `cfg_query::barrier_separates` query: when the two writes are NOT
// separated by a barrier, the first write is the dead-overwrite hazard.
//
// Detection (AST + CFG):
//   1. Walk the AST collecting every assignment whose LHS is
//      `gs_name[index_text]` for a recorded groupshared name. The
//      `index_text` is the verbatim text of the index expression (the
//      analysis is cell-equality on text, not value).
//   2. Pair adjacent writes (in source order) to the same `(name,
//      index_text)` within the same enclosing function body.
//   3. Ask `cfg_query::barrier_separates` whether the CFG places a barrier
//      between them. If not, fire on the first (dead) write.
//
// The text-equality cell match is conservative against indices like
// `tid + 1` vs `tid+1` (same value, different text -- treated as different
// cells, no fire). Authors who refactor whitespace can hit a false
// negative; the rule prefers misses to false positives, per the
// conservatism contract documented in `light_dataflow.hpp`.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "groupshared-overwrite-before-barrier";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo) {
        return {};
    }
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node)) {
        return {};
    }
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

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

void collect_groupshared_names(::TSNode node,
                               std::string_view bytes,
                               std::unordered_set<std::string>& out) {
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
                    out.insert(std::string{name});
                }
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_groupshared_names(::ts_node_child(node, i), bytes, out);
    }
}

struct WriteSite {
    std::string cell_key;  // "<name>[<index_text>]"
    ::TSNode assign_node;
    ::TSNode lhs_subscript_node;
    std::string receiver;
    std::uint32_t enclosing_function_id = 0U;
};

/// Walk up to the enclosing `function_definition` and return its node-id-ish
/// identity (we use the start_byte as an approximation; two distinct
/// functions cannot share a start byte). Returns 0 when no enclosing
/// function is found (top-level expression).
[[nodiscard]] std::uint32_t enclosing_function_key(::TSNode n) noexcept {
    ::TSNode cur = n;
    while (!::ts_node_is_null(cur)) {
        const auto kind = node_kind(cur);
        if (kind == "function_definition" || kind == "method_definition") {
            return static_cast<std::uint32_t>(::ts_node_start_byte(cur));
        }
        cur = ::ts_node_parent(cur);
    }
    return 0U;
}

void scan_writes(::TSNode node,
                 std::string_view bytes,
                 const std::unordered_set<std::string>& names,
                 std::vector<WriteSite>& out_sites) {
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
                ::TSNode index = ::ts_node_child_by_field_name(lhs, "index", 5);
                if (::ts_node_is_null(index)) {
                    const std::uint32_t cnt = ::ts_node_child_count(lhs);
                    for (std::uint32_t i = cnt; i-- > 0U;) {
                        const ::TSNode c = ::ts_node_child(lhs, i);
                        const auto k = node_kind(c);
                        if (k != "[" && k != "]") {
                            index = c;
                            break;
                        }
                    }
                }
                const auto index_text = node_text(index, bytes);
                std::string cell_key = std::string{receiver_text};
                cell_key.push_back('[');
                cell_key.append(index_text);
                cell_key.push_back(']');

                WriteSite ws;
                ws.cell_key = std::move(cell_key);
                ws.assign_node = node;
                ws.lhs_subscript_node = lhs;
                ws.receiver = std::string{receiver_text};
                ws.enclosing_function_id = enclosing_function_key(node);
                out_sites.push_back(std::move(ws));
            }
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_writes(::ts_node_child(node, i), bytes, names, out_sites);
    }
}

class GroupsharedOverwriteBeforeBarrier : public Rule {
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
        collect_groupshared_names(root, bytes, names);
        if (names.empty()) {
            return;
        }

        std::vector<WriteSite> sites;
        scan_writes(root, bytes, names, sites);
        if (sites.size() < 2U) {
            return;
        }

        // Pair earliest write to a `(function, cell_key)` with the next write
        // to the same cell in the same function. If the CFG does NOT place a
        // barrier between them, the first write is the dead overwrite.
        //
        // We index by `(enclosing_function_id, cell_key)` and remember the
        // most recent write for each key. When a second write arrives, we
        // ask the CFG; if it fires we emit on the earlier (dead) write and
        // reset the slot to the second write so a third overwrite chains.
        std::unordered_map<std::string, std::size_t> last_write_idx;
        for (std::size_t i = 0; i < sites.size(); ++i) {
            const auto& w = sites[i];
            std::string key = std::to_string(w.enclosing_function_id);
            key.push_back('#');
            key.append(w.cell_key);
            const auto it = last_write_idx.find(key);
            if (it != last_write_idx.end()) {
                const auto& prev = sites[it->second];
                const Span prev_span{
                    .source = tree.source_id(),
                    .bytes = tree.byte_range(prev.lhs_subscript_node),
                };
                const Span cur_span{
                    .source = tree.source_id(),
                    .bytes = tree.byte_range(w.lhs_subscript_node),
                };
                if (!util::barrier_separates(cfg, prev_span, cur_span)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span = Span{
                        .source = tree.source_id(),
                        .bytes = tree.byte_range(prev.assign_node),
                    };
                    diag.message = std::string{"groupshared write to `"} + prev.cell_key +
                                   "` is overwritten on the same path with no intervening "
                                   "`GroupMemoryBarrier*` -- the first store is unobservable "
                                   "to other threads; delete it or insert a barrier";
                    ctx.emit(std::move(diag));
                }
            }
            last_write_idx[key] = i;
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_overwrite_before_barrier() {
    return std::make_unique<GroupsharedOverwriteBeforeBarrier>();
}

}  // namespace hlsl_clippy::rules
