// groupshared-first-read-without-barrier
//
// Detects a read of a `groupshared` cell whose value some thread *has*
// written, when the CFG places no `GroupMemoryBarrier*` between the
// dominating write and the read. This is the cross-lane race: another
// thread wrote the cell, the reading thread has no synchronisation
// guaranteeing visibility, and the value seen is undefined under the HLSL
// memory model.
//
// Distinct from `groupshared-uninitialized-read` (which fires when *no*
// thread has written the cell on any path); this rule fires when a writer
// exists but no barrier separates it from the reader.
//
// Stage: `ControlFlow`. The rule pairs an AST scan that locates write/read
// access pairs against the same `(name, index_text)` cell with a
// `cfg_query::barrier_separates` query.
//
// Severity: `Error`. The HLSL memory model produces undefined results
// without the barrier; vendor schedulers expose this as wrong-output bugs
// that emerge at workgroup sizes greater than one wave. Per the doc page,
// `applicability = none`: the diagnostic identifies the racing read but
// the barrier insertion site requires producer-consumer reasoning the
// rule cannot automate.
//
// Detection (AST + CFG):
//   1. Collect every `groupshared` declaration's variable name.
//   2. Walk the AST collecting every assignment whose LHS is
//      `gs_name[index_text]` (a write) and every standalone read of the
//      same shape that does NOT appear on the LHS of an assignment.
//   3. For each read, check if any earlier write to the same cell exists
//      in source order in the same enclosing function. If yes, ask
//      `cfg_query::barrier_separates`. If no barrier, emit error.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"
#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-first-read-without-barrier";
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

struct AccessSite {
    std::string cell_key;
    ::TSNode subscript_node;
    std::uint32_t fn_id = 0U;
    bool is_write = false;
};

[[nodiscard]] bool is_lhs_of_assignment(::TSNode subscript) noexcept {
    const ::TSNode parent = ::ts_node_parent(subscript);
    if (::ts_node_is_null(parent)) {
        return false;
    }
    if (node_kind(parent) != "assignment_expression") {
        return false;
    }
    const ::TSNode lhs = ::ts_node_child_by_field_name(parent, "left", 4);
    return !::ts_node_is_null(lhs) && ::ts_node_eq(lhs, subscript);
}

[[nodiscard]] bool is_addressed_by_atomic_or_outparam(::TSNode subscript) noexcept {
    // `Interlocked*` mutates its first arg in-place; treat it as a write site
    // for the purposes of this rule (the read-after-atomic case is the
    // common one; the rule still fires correctly because the atomic itself
    // counts as a write, not a racing read).
    const ::TSNode parent = ::ts_node_parent(subscript);
    if (::ts_node_is_null(parent)) {
        return false;
    }
    if (node_kind(parent) != "argument_list") {
        return false;
    }
    const ::TSNode call = ::ts_node_parent(parent);
    if (::ts_node_is_null(call) || node_kind(call) != "call_expression") {
        return false;
    }
    const ::TSNode fn = ::ts_node_child_by_field_name(call, "function", 8);
    if (::ts_node_is_null(fn)) {
        return false;
    }
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(fn));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(fn));
    if (hi <= lo) {
        return false;
    }
    // We just need a name-prefix check; node_text would require bytes.
    // Walk parent text via tree handle is the caller's job; here we return
    // false and let the caller make the policy decision. (This helper is a
    // placeholder kept for future refinement.)
    return false;
}

void scan_accesses(::TSNode node,
                   std::string_view bytes,
                   const std::unordered_set<std::string>& names,
                   std::vector<AccessSite>& out) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "subscript_expression") {
        ::TSNode receiver = ::ts_node_child_by_field_name(node, "argument", 8);
        if (::ts_node_is_null(receiver)) {
            receiver = ::ts_node_child(node, 0U);
        }
        const auto receiver_text = node_text(receiver, bytes);
        if (!receiver_text.empty() && names.contains(std::string{receiver_text})) {
            ::TSNode index = ::ts_node_child_by_field_name(node, "index", 5);
            if (::ts_node_is_null(index)) {
                const std::uint32_t cnt = ::ts_node_child_count(node);
                for (std::uint32_t i = cnt; i-- > 0U;) {
                    const ::TSNode c = ::ts_node_child(node, i);
                    const auto k = node_kind(c);
                    if (k != "[" && k != "]") {
                        index = c;
                        break;
                    }
                }
            }
            const auto index_text = node_text(index, bytes);

            AccessSite site;
            site.cell_key = std::string{receiver_text};
            site.cell_key.push_back('[');
            site.cell_key.append(index_text);
            site.cell_key.push_back(']');
            site.subscript_node = node;
            site.fn_id = enclosing_function_key(node);
            site.is_write = is_lhs_of_assignment(node) || is_addressed_by_atomic_or_outparam(node);
            out.push_back(std::move(site));
        }
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_accesses(::ts_node_child(node, i), bytes, names, out);
    }
}

class GroupsharedFirstReadWithoutBarrier : public Rule {
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

        std::vector<AccessSite> sites;
        scan_accesses(root, bytes, names, sites);
        if (sites.size() < 2U) {
            return;
        }

        // Per-(function, cell) pair the earliest write with later reads. We
        // compute, for every read, whether some prior write in the same
        // function (regardless of cell index match) is barrier-separated
        // from it. We use cell-key match to scope the "writer exists" check
        // -- this is conservative against `gs[a] = ...; ... = gs[b]` where
        // `a` and `b` may alias at runtime; we miss those cases (false
        // negative) rather than fire on writes to disjoint cells.
        std::unordered_map<std::string, std::vector<std::size_t>> writes_by_key;
        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (sites[i].is_write) {
                std::string k = std::to_string(sites[i].fn_id);
                k.push_back('#');
                k.append(sites[i].cell_key);
                writes_by_key[k].push_back(i);
            }
        }

        // Per-name (not per-cell) writes: a writer to *any* cell of `gs`
        // makes any read of `gs` susceptible to the cross-lane race. We
        // track these separately so that `gs[gi] = ...; ... = gs[(gi+1)%64]`
        // (different cells, classic race) still fires.
        std::unordered_map<std::string, std::vector<std::size_t>> writes_by_name;
        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (sites[i].is_write) {
                // Extract the receiver name from the cell_key prefix.
                const auto& key = sites[i].cell_key;
                const auto bracket = key.find('[');
                if (bracket != std::string::npos) {
                    std::string nk = std::to_string(sites[i].fn_id);
                    nk.push_back('#');
                    nk.append(key.substr(0, bracket));
                    writes_by_name[nk].push_back(i);
                }
            }
        }

        for (std::size_t i = 0; i < sites.size(); ++i) {
            if (sites[i].is_write) {
                continue;
            }
            const auto& read = sites[i];
            const auto bracket = read.cell_key.find('[');
            if (bracket == std::string::npos) {
                continue;
            }
            std::string nk = std::to_string(read.fn_id);
            nk.push_back('#');
            nk.append(read.cell_key.substr(0, bracket));

            const auto it = writes_by_name.find(nk);
            if (it == writes_by_name.end()) {
                continue;
            }
            // Pick the earliest preceding write in source order.
            std::size_t earliest_writer_idx = sites.size();
            const auto read_lo =
                static_cast<std::uint32_t>(::ts_node_start_byte(read.subscript_node));
            for (const auto widx : it->second) {
                const auto wlo =
                    static_cast<std::uint32_t>(::ts_node_start_byte(sites[widx].subscript_node));
                if (wlo < read_lo &&
                    (earliest_writer_idx == sites.size() ||
                     wlo < ::ts_node_start_byte(sites[earliest_writer_idx].subscript_node))) {
                    earliest_writer_idx = widx;
                }
            }
            if (earliest_writer_idx == sites.size()) {
                continue;  // no writer precedes this read
            }
            const auto& w = sites[earliest_writer_idx];
            const Span write_span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(w.subscript_node),
            };
            const Span read_span{
                .source = tree.source_id(),
                .bytes = tree.byte_range(read.subscript_node),
            };
            if (!util::barrier_separates(cfg, write_span, read_span)) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = read_span;
                diag.message = std::string{"groupshared read of `"} + read.cell_key +
                               "` is not separated by `GroupMemoryBarrier*` from a prior "
                               "write to the same array on this CFG path; cross-lane reads "
                               "are unordered without a barrier and produce undefined "
                               "values once the workgroup spans more than one wave";
                ctx.emit(std::move(diag));
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_first_read_without_barrier() {
    return std::make_unique<GroupsharedFirstReadWithoutBarrier>();
}

}  // namespace shader_clippy::rules
