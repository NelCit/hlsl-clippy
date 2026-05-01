// groupshared-uninitialized-read
//
// Detects reads of a `groupshared` cell on a CFG path before any thread has
// written to it. Per the HLSL memory model, the cell's contents are
// undefined until at least one writer + barrier has executed.
//
// Stage: ControlFlow. Uses `light_dataflow::groupshared_read_before_write`
// (forward-compatible stub returning `false` until the engine grows per-cell
// first-access tracking, per ADR 0013 §"Decision Outcome" point 6). The
// rule wires the API today; precision tightens as the engine catches up.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/light_dataflow.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "groupshared-uninitialized-read";
constexpr std::string_view k_category = "workgroup";

[[nodiscard]] std::string_view node_kind(::TSNode node) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const char* t = ::ts_node_type(node);
    return t != nullptr ? std::string_view{t} : std::string_view{};
}

[[nodiscard]] std::string_view node_text(::TSNode node, std::string_view bytes) noexcept {
    if (::ts_node_is_null(node))
        return {};
    const auto lo = static_cast<std::uint32_t>(::ts_node_start_byte(node));
    const auto hi = static_cast<std::uint32_t>(::ts_node_end_byte(node));
    if (lo > bytes.size() || hi > bytes.size() || hi < lo)
        return {};
    return bytes.substr(lo, hi - lo);
}

[[nodiscard]] bool is_id_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// Locate every `groupshared <name>` declaration's name span in the source
/// text.
[[nodiscard]] std::vector<Span> collect_gs_decl_spans(std::string_view bytes, SourceId src) {
    std::vector<Span> out;
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
        // Walk to the identifier token just before `[` or `;`.
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
            out.push_back(Span{.source = src,
                               .bytes = ByteSpan{static_cast<std::uint32_t>(name_start),
                                                 static_cast<std::uint32_t>(name_end)}});
        }
        pos = boundary;
    }
    return out;
}

class GroupsharedUninitializedRead : public Rule {
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
        const auto decls = collect_gs_decl_spans(bytes, tree.source_id());
        for (const auto decl : decls) {
            // Forward-compatible stub: `groupshared_read_before_write`
            // currently returns false until the engine grows per-cell
            // first-access tracking. The rule wires the API so it tightens
            // automatically when the engine catches up.
            if (!util::groupshared_read_before_write(cfg, decl))
                continue;
            Diagnostic diag;
            diag.code = std::string{k_rule_id};
            diag.severity = Severity::Warning;
            diag.primary_span = decl;
            diag.message = std::string{
                "groupshared cell read before any thread writes it on some "
                "CFG path -- the cell's contents are undefined; either "
                "initialise unconditionally before the first read, or gate "
                "the read on a barrier-synchronised writer"};

            Fix fix;
            fix.machine_applicable = false;
            fix.description = std::string{
                "initialise the groupshared cell at thread-0 (gated by "
                "tid.x == 0) followed by a `GroupMemoryBarrierWithGroupSync` "
                "before any read"};
            diag.fixes.push_back(std::move(fix));

            ctx.emit(std::move(diag));
        }
    }
};

}  // namespace

std::unique_ptr<Rule> make_groupshared_uninitialized_read() {
    return std::make_unique<GroupsharedUninitializedRead>();
}

}  // namespace hlsl_clippy::rules
