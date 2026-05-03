// reordercoherent-uav-missing-barrier
//
// Detects a UAV qualified `[reordercoherent]` whose write site is followed
// by a `MaybeReorderThread` and a subsequent read on the same UAV, without
// an intervening `DeviceMemoryBarrier` / `GroupMemoryBarrier` /
// `AllMemoryBarrier`. Per ADR 0010 §Phase 4 (rule #8) and proposal 0027,
// `[reordercoherent]` is the SER-specific cousin of `globallycoherent`: it
// promises the runtime that the application has placed an explicit fence
// around the reorder; missing that fence is silently-wrong UB on
// NVIDIA Ada Lovelace, AMD RDNA 4, and Vulkan SER.
//
// Stage: ControlFlow (forward-compatible-stub for Phase 4 cross-reorder
// barrier reachability).
//
// The full rule needs CFG-path queries that cross the `MaybeReorderThread`
// call site -- specifically, "every path from the write block to the read
// block passes through a barrier block AND through the reorder block". The
// helper `cfg_query::barrier_separates` covers the barrier-presence half;
// the reorder-point-as-CFG-edge half is not yet exposed by sub-phase 4a.
// This stub fires on the textual shape: a `[reordercoherent]` declaration
// in the source, plus a function that contains a write-then-reorder-then-
// read sequence with no barrier in between. Once 4a's CFG grows reorder-
// point edges, the stub upgrades to a precise barrier-separates query.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

constexpr std::string_view k_rule_id = "reordercoherent-uav-missing-barrier";
constexpr std::string_view k_category = "ser";
constexpr std::string_view k_attribute = "[reordercoherent]";
constexpr std::string_view k_reorder_call = "MaybeReorderThread";

/// True when `text` contains any of the HLSL barrier intrinsics that
/// satisfy the `[reordercoherent]` synchronisation contract.
[[nodiscard]] bool contains_barrier(std::string_view text) noexcept {
    return text.find("DeviceMemoryBarrier") != std::string_view::npos ||
           text.find("AllMemoryBarrier") != std::string_view::npos ||
           text.find("GroupMemoryBarrier") != std::string_view::npos;
}

/// Find every `[reordercoherent]`-attributed UAV name in the source. The
/// names are matched textually; once Slang reflection exposes the qualifier,
/// the lookup tightens to a reflection query.
void collect_reordercoherent_names(std::string_view bytes, std::vector<std::string_view>& out) {
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        const auto pos = bytes.find(k_attribute, cursor);
        if (pos == std::string_view::npos) {
            break;
        }
        // Walk forward past the attribute, the resource type, and pick out
        // the identifier preceding the `;` or `:`. The grammar exposes
        // declaration nodes too, but the textual scan is sufficient for the
        // stub.
        std::size_t end = bytes.find(';', pos);
        if (end == std::string_view::npos) {
            break;
        }
        const auto decl = bytes.substr(pos, end - pos);
        // Find the last identifier-like token before either `:` (register
        // binding) or end-of-decl.
        std::size_t scan_end = decl.find(':');
        if (scan_end == std::string_view::npos) {
            scan_end = decl.size();
        }
        // Scan backward over identifier chars from scan_end - 1.
        std::size_t e = scan_end;
        while (e > 0 && (decl[e - 1] == ' ' || decl[e - 1] == '\t')) {
            --e;
        }
        std::size_t s = e;
        while (s > 0) {
            const char c = decl[s - 1];
            const bool is_id = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9') || c == '_';
            if (!is_id) {
                break;
            }
            --s;
        }
        if (s < e) {
            out.push_back(decl.substr(s, e - s));
        }
        cursor = end + 1;
    }
}

/// True when `fn_text` contains, in source order, a write to `name`, then a
/// `MaybeReorderThread`, then a read of `name`, with no barrier between the
/// write and the read. Approximation only; the full rule walks CFG paths.
[[nodiscard]] bool unsynchronised_window(std::string_view fn_text, std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    // Locate first textual write `name[`...`] =` (assignment, including
    // compound forms `+=` etc.).
    std::size_t cursor = 0;
    while (cursor < fn_text.size()) {
        const auto npos = fn_text.find(name, cursor);
        if (npos == std::string_view::npos) {
            return false;
        }
        // Identifier-char boundary check (avoid matching a substring).
        if (npos > 0) {
            const char prev = fn_text[npos - 1];
            const bool is_id_prev = (prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') ||
                                    (prev >= '0' && prev <= '9') || prev == '_';
            if (is_id_prev) {
                cursor = npos + name.size();
                continue;
            }
        }
        if (npos + name.size() < fn_text.size()) {
            const char next = fn_text[npos + name.size()];
            const bool is_id_next = (next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
                                    (next >= '0' && next <= '9') || next == '_';
            if (is_id_next) {
                cursor = npos + name.size();
                continue;
            }
        }
        // Look forward for `[`...`] =` to call this a write site.
        const auto bracket = fn_text.find('[', npos);
        if (bracket == std::string_view::npos) {
            return false;
        }
        const auto close = fn_text.find(']', bracket);
        if (close == std::string_view::npos) {
            return false;
        }
        // Check the next non-space char after `]` is `=`, `+=` etc., but
        // not `==`.
        std::size_t i = close + 1;
        while (i < fn_text.size() && (fn_text[i] == ' ' || fn_text[i] == '\t')) {
            ++i;
        }
        if (i < fn_text.size() && fn_text[i] == '=' &&
            (i + 1 >= fn_text.size() || fn_text[i + 1] != '=')) {
            // Found a write at `[npos, close]`. Now look forward for the
            // reorder + a subsequent read of `name`.
            const auto reorder = fn_text.find(k_reorder_call, close);
            if (reorder == std::string_view::npos) {
                cursor = close;
                continue;
            }
            const auto read = fn_text.find(name, reorder);
            if (read == std::string_view::npos) {
                return false;
            }
            const auto window = fn_text.substr(npos, read - npos);
            if (!contains_barrier(window)) {
                return true;
            }
            cursor = read + name.size();
        } else {
            cursor = close + 1;
        }
    }
    return false;
}

void walk(::TSNode node,
          std::string_view bytes,
          const std::vector<std::string_view>& names,
          const AstTree& tree,
          const ControlFlowInfo& cfg,
          RuleContext& ctx) {
    if (::ts_node_is_null(node) || names.empty()) {
        return;
    }
    if (node_kind(node) == "function_definition") {
        const auto fn_text = node_text(node, bytes);
        for (const auto name : names) {
            if (unsynchronised_window(fn_text, name)) {
                // Best-effort CFG cross-check: if the cfg query exposes a
                // barrier between the function entry and the function exit,
                // skip the diagnostic (means a barrier dominates the path).
                // The stub uses the function's own span as a coarse anchor;
                // refinement lands with reorder-point edges in 4a.
                const auto fn_span =
                    Span{.source = tree.source_id(), .bytes = tree.byte_range(node)};
                if (util::barrier_separates(cfg, fn_span, fn_span)) {
                    continue;
                }
                Diagnostic diag;
                diag.code = std::string{k_rule_id};
                diag.severity = Severity::Error;
                diag.primary_span = fn_span;
                diag.message = std::string{"`[reordercoherent]` UAV `"} + std::string{name} +
                               "` is read after `MaybeReorderThread` on a path that "
                               "wrote it before the reorder, without an intervening "
                               "barrier -- the reorder shuffles lanes between the L1 "
                               "cache lines (proposal 0027 SER); add "
                               "`DeviceMemoryBarrier()` before the reorder";
                ctx.emit(std::move(diag));
                break;
            }
        }
    }

    const std::uint32_t count = ::ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk(::ts_node_child(node, i), bytes, names, tree, cfg, ctx);
    }
}

class ReordercoherentUavMissingBarrier : public Rule {
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
        std::vector<std::string_view> names;
        collect_reordercoherent_names(tree.source_bytes(), names);
        walk(::ts_tree_root_node(tree.raw_tree()), tree.source_bytes(), names, tree, cfg, ctx);
    }
};

}  // namespace

std::unique_ptr<Rule> make_reordercoherent_uav_missing_barrier() {
    return std::make_unique<ReordercoherentUavMissingBarrier>();
}

}  // namespace shader_clippy::rules
