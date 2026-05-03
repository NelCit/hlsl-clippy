// groupshared-write-then-no-barrier-read
//
// Detects a thread reading a `groupshared` cell that another thread wrote
// without a `GroupMemoryBarrier*` separating the two accesses. This is UB
// per the HLSL memory model; without the barrier the reader may observe
// stale or partially-updated values.
//
// Stage: ControlFlow. Uses `cfg_query::barrier_separates` to verify a
// barrier exists between any matching write/read pair.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "rules/util/ast_helpers.hpp"
#include "rules/util/cfg_query.hpp"
#include "shader_clippy/control_flow.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace shader_clippy::rules {
namespace {

using util::is_id_char;
using util::node_kind;
using util::node_text;

constexpr std::string_view k_rule_id = "groupshared-write-then-no-barrier-read";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] std::vector<std::string> collect_gs_names(std::string_view bytes) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto found = bytes.find("groupshared", pos);
        if (found == std::string_view::npos)
            return out;
        if (found > 0 && is_id_char(bytes[found - 1])) {
            pos = found + 1;
            continue;
        }
        std::size_t i = found + std::string_view{"groupshared"}.size();
        if (i < bytes.size() && is_id_char(bytes[i])) {
            pos = i;
            continue;
        }
        std::size_t boundary = i;
        while (boundary < bytes.size() && bytes[boundary] != ';' && bytes[boundary] != '[' &&
               bytes[boundary] != '{') {
            ++boundary;
        }
        std::size_t name_end = boundary;
        while (name_end > i && (bytes[name_end - 1] == ' ' || bytes[name_end - 1] == '\t'))
            --name_end;
        std::size_t name_start = name_end;
        while (name_start > i && is_id_char(bytes[name_start - 1]))
            --name_start;
        if (name_end > name_start) {
            out.emplace_back(bytes.substr(name_start, name_end - name_start));
        }
        pos = boundary;
    }
    return out;
}

/// Collect every `gs[expr]` subscript node, paired with whether the access is
/// on the LHS of an assignment (a write) or otherwise (a read).
struct GsAccess {
    ::TSNode node;
    bool is_write;
};

void collect_gs_accesses(::TSNode node,
                         std::string_view bytes,
                         const std::vector<std::string>& gs_names,
                         std::vector<GsAccess>& out,
                         bool parent_is_lhs) {
    if (::ts_node_is_null(node))
        return;
    if (node_kind(node) == "subscript_expression") {
        const auto recv = ::ts_node_named_child(node, 0);
        const auto recv_text = node_text(recv, bytes);
        for (const auto& name : gs_names) {
            if (recv_text == name) {
                out.push_back({node, parent_is_lhs});
                break;
            }
        }
    }
    if (node_kind(node) == "assignment_expression") {
        // Children: lhs operator rhs. Mark first child LHS.
        const std::uint32_t count = ::ts_node_child_count(node);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto child = ::ts_node_child(node, i);
            // Heuristic: the first named child is the LHS.
            collect_gs_accesses(child, bytes, gs_names, out, i == 0);
        }
        return;
    }
    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        collect_gs_accesses(::ts_node_child(node, i), bytes, gs_names, out, false);
    }
}

class GroupsharedWriteThenNoBarrierRead : public Rule {
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
        const auto bytes = tree.source_bytes();
        const auto gs_names = collect_gs_names(bytes);
        if (gs_names.empty())
            return;
        std::vector<GsAccess> accesses;
        collect_gs_accesses(::ts_tree_root_node(tree.raw_tree()), bytes, gs_names, accesses, false);
        // For every (write, read) pair in document order, fire if no barrier
        // separates them.
        for (std::size_t w = 0; w < accesses.size(); ++w) {
            if (!accesses[w].is_write)
                continue;
            const auto write_span =
                Span{.source = tree.source_id(), .bytes = tree.byte_range(accesses[w].node)};
            for (std::size_t r = w + 1; r < accesses.size(); ++r) {
                if (accesses[r].is_write)
                    continue;
                const auto read_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(accesses[r].node)};
                if (util::barrier_separates(cfg, write_span, read_span))
                    continue;
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Warning;
                diag.primary_span = read_span;
                diag.message = std::string{
                    "groupshared read after a cross-thread write with no "
                    "GroupMemoryBarrier* between them -- the reader may observe "
                    "stale or partially-updated values; UB per the HLSL memory "
                    "model"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "insert `GroupMemoryBarrierWithGroupSync()` between the "
                    "writer and the reader; without it the LDS visibility is "
                    "implementation-defined"};
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
                // Only report once per read site.
                break;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_write_then_no_barrier_read() {
    return std::make_unique<GroupsharedWriteThenNoBarrierRead>();
}

}  // namespace shader_clippy::rules
