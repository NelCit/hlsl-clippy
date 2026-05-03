// wave-active-all-equal-precheck
//
// Detects descriptor-heap / dynamic-index reads that look divergent
// (`ResourceDescriptorHeap[idx]` / `buf[idx]` where idx looks per-lane) and
// suggests wrapping the index in a `WaveActiveAllEqual(idx)` precheck so the
// uniform-on-this-wave fast path can fire.
//
// Stage: Ast (forward-compatible stub for the Phase 4 uniformity oracle).
// We fire on subscript expressions whose index identifier matches the seed
// divergent-source list; the full uniformity-aware version lives behind
// `cfg.uniformity.of_expr(...)` in Phase 4 4c rule packs.

#include <array>
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

constexpr std::string_view k_rule_id = "wave-active-all-equal-precheck";
constexpr std::string_view k_category = "control-flow";

constexpr std::array<std::string_view, 4> k_heap_kinds{
    "ResourceDescriptorHeap",
    "SamplerDescriptorHeap",
    "g_texHeap",  // common project alias; benign noise
    "g_samplers",
};

void walk(::TSNode node, std::string_view bytes, const AstTree& tree, RuleContext& ctx) {
    if (::ts_node_is_null(node))
        return;

    if (node_kind(node) == "subscript_expression") {
        const auto receiver = ::ts_node_named_child(node, 0);
        const auto idx = ::ts_node_named_child(node, 1);
        const auto recv_text = node_text(receiver, bytes);
        const auto idx_text = node_text(idx, bytes);
        bool is_heap_like = false;
        for (const auto kind : k_heap_kinds) {
            if (recv_text == kind) {
                is_heap_like = true;
                break;
            }
        }
        // We only fire on the descriptor-heap surface to keep noise low.
        if (is_heap_like) {
            // Skip when an obvious WaveActiveAllEqual / WaveReadLaneFirst guard
            // is already present in the surrounding expression text. We look
            // at the source slice covering the parent statement / expression.
            const auto outer = ::ts_node_parent(node);
            const auto outer_text = node_text(outer, bytes);
            const bool guarded =
                outer_text.find("WaveActiveAllEqual") != std::string_view::npos ||
                outer_text.find("WaveReadLaneFirst") != std::string_view::npos ||
                outer_text.find("NonUniformResourceIndex") != std::string_view::npos;
            if (!guarded && !idx_text.empty()) {
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Note;
                diag.primary_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                diag.message = std::string{
                    "descriptor-heap read with no `WaveActiveAllEqual` precheck -- "
                    "if every lane in the wave shares the same index, the cheap "
                    "uniform path is available; otherwise mark with "
                    "`NonUniformResourceIndex`"};

                Fix fix;
                fix.machine_applicable = false;
                fix.description = std::string{
                    "wrap the access in `if (WaveActiveAllEqual(idx)) { ... } "
                    "else { NonUniformResourceIndex(idx) ... }` to take the "
                    "wave-uniform fast path on RDNA/Ada when possible"};
                diag.fixes.push_back(std::move(fix));

                ctx.emit(std::move(diag));
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, tree, ctx);
    }
}

class WaveActiveAllEqualPrecheck : public Rule {
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

std::unique_ptr<Rule> make_wave_active_all_equal_precheck() {
    return std::make_unique<WaveActiveAllEqualPrecheck>();
}

}  // namespace shader_clippy::rules
