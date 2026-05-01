// flatten-on-uniform-branch
//
// Detects `[flatten]` applied to an `if` / `else` whose condition is
// dynamically uniform across the wave. On a uniform branch, `[flatten]`
// forces both arms to execute every wave (one ALU instruction per arm
// instruction) when `[branch]` (or no attribute) would let the wave skip
// the inactive arm entirely. Per ADR 0011 §Phase 4 (rule pack C) and the
// rule's doc page.
//
// Stage: `ControlFlow`. The rule pairs an AST scan over `if_statement`
// nodes that carry a `[flatten]` attribute prefix with a
// `uniformity::is_uniform` (or `is_loop_invariant`) query on the branch
// condition. When the condition is uniform-or-loop-invariant, the
// diagnostic fires.
//
// Detection (AST + uniformity oracle):
//   1. Walk every `if_statement` in the source.
//   2. Inspect the bytes immediately preceding the `if` keyword (or the
//      sibling `attribute` node tree-sitter exposes) for `[flatten]`. If
//      the attribute is not `[flatten]`, skip.
//   3. Ask the uniformity oracle whether the branch condition span is
//      `Uniform` or `LoopInvariant`. If so, emit on the `if` statement.
//
// Conservatism contract: `Unknown` from the oracle does NOT trigger a
// fire. The fix is `suggestion`-only because switching `[flatten]` to
// `[branch]` may surface compiler-version differences in lowering.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "rules/util/uniformity.hpp"

#include "parser_internal.hpp"
#include "rules.hpp"

namespace hlsl_clippy::rules {
namespace {

constexpr std::string_view k_rule_id = "flatten-on-uniform-branch";
constexpr std::string_view k_category = "control-flow";
constexpr std::string_view k_flatten_attr = "[flatten]";

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

/// True when the byte range immediately preceding `if_lo` (back to the
/// previous statement-terminating character or block opener) carries a
/// `[flatten]` attribute. This walks backward over whitespace and looks
/// for the literal `[flatten]` suffix.
[[nodiscard]] bool preceded_by_flatten(std::string_view bytes, std::uint32_t if_lo) noexcept {
    if (if_lo == 0U || if_lo > bytes.size()) {
        return false;
    }
    // Walk backward over whitespace.
    std::uint32_t i = if_lo;
    while (i > 0U) {
        const char c = bytes[i - 1U];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            --i;
            continue;
        }
        break;
    }
    // Now look for `[flatten]` ending at i.
    if (i < k_flatten_attr.size()) {
        return false;
    }
    return bytes.substr(i - k_flatten_attr.size(), k_flatten_attr.size()) == k_flatten_attr;
}

/// True when the parent of `node` is an `attributed_statement` and the
/// attribute text contains `[flatten]`. tree-sitter-hlsl wraps attributed
/// statements with a sibling attribute node in some grammar versions; we
/// support both layouts.
[[nodiscard]] bool has_flatten_attribute(::TSNode if_node, std::string_view bytes) noexcept {
    // Try the direct text-preceding-the-`if` heuristic first.
    const auto if_lo = static_cast<std::uint32_t>(::ts_node_start_byte(if_node));
    if (preceded_by_flatten(bytes, if_lo)) {
        return true;
    }
    // Fall back to walking up via `ts_node_parent`. If the parent is an
    // attributed-statement-like node and its text contains `[flatten]`
    // before the if's start, accept.
    const ::TSNode parent = ::ts_node_parent(if_node);
    if (::ts_node_is_null(parent)) {
        return false;
    }
    const auto parent_text = node_text(parent, bytes);
    const auto parent_lo = static_cast<std::uint32_t>(::ts_node_start_byte(parent));
    if (if_lo <= parent_lo) {
        return false;
    }
    const std::size_t prefix_len = if_lo - parent_lo;
    if (prefix_len > parent_text.size()) {
        return false;
    }
    const auto prefix = parent_text.substr(0U, prefix_len);
    return prefix.find(k_flatten_attr) != std::string_view::npos;
}

void scan_ifs(::TSNode node,
              std::string_view bytes,
              const ControlFlowInfo& cfg,
              const AstTree& tree,
              RuleContext& ctx) {
    if (::ts_node_is_null(node)) {
        return;
    }
    if (node_kind(node) == "if_statement") {
        if (has_flatten_attribute(node, bytes)) {
            const ::TSNode cond = ::ts_node_child_by_field_name(node, "condition", 9);
            if (!::ts_node_is_null(cond)) {
                const Span cond_span{
                    .source = tree.source_id(),
                    .bytes = tree.byte_range(cond),
                };
                if (util::is_uniform(cfg, cond_span) || util::is_loop_invariant(cfg, cond_span)) {
                    Diagnostic diag;
                    diag.code = std::string{k_rule_id};
                    diag.severity = Severity::Warning;
                    diag.primary_span =
                        Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                    diag.message = std::string{
                        "`[flatten]` on a wave-uniform branch is a pessimisation -- both arms "
                        "execute every wave when `[branch]` (or no attribute) would let the wave "
                        "skip the inactive arm entirely; switch to `[branch]` to recover the dead "
                        "arm's instructions"};
                    ctx.emit(std::move(diag));
                }
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        scan_ifs(::ts_node_child(node, i), bytes, cfg, tree, ctx);
    }
}

class FlattenOnUniformBranch : public Rule {
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
        scan_ifs(root, tree.source_bytes(), cfg, tree, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_flatten_on_uniform_branch() {
    return std::make_unique<FlattenOnUniformBranch>();
}

}  // namespace hlsl_clippy::rules
