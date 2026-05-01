// Internal CFG engine -- core-private API used by `lint.cpp`.
//
// The engine sits between the lint orchestrator and the per-source CFG +
// uniformity analyzer. It owns a per-source cache (lint-run scoped) keyed
// by `SourceId`, wraps `cfg_builder` + `compute_dominators` +
// `analyse_uniformity` into one entry point, and returns
// `std::expected<ControlFlowInfo, Diagnostic>` to the orchestrator.
//
// Per ADR 0013 §"Decision Outcome" point 4, the engine is a process
// singleton accessed via `instance()`, mirroring the reflection engine's
// shape. Cache lifetime is "as long as the engine lives" -- callers can
// drop the cache via `clear_cache()` between lint runs that want a fresh
// build (the unit tests need this).

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <shared_mutex>
#include <utility>
#include <vector>

// std::flat_map is C++23 (P0429) but landed in libstdc++ only at GCC 15.1
// and in libc++ only at libc++ 20. Ubuntu 24.04 (Linux CI) ships GCC 13's
// libstdc++ which lacks <flat_map>; ADR 0004 §"Selective C++26 adopt-now
// (feature-test gated)" pattern applies. Cache cardinality is small
// (typically < 100 entries per lint run) so std::map is fine here.
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
#include <flat_map>
namespace hlsl_clippy::control_flow::detail {
template<typename K, typename V>
using CfgCacheMap = std::flat_map<K, V>;
}  // namespace hlsl_clippy::control_flow::detail
#else
#include <map>
namespace hlsl_clippy::control_flow::detail {
template<typename K, typename V>
using CfgCacheMap = std::map<K, V>;
}  // namespace hlsl_clippy::control_flow::detail
#endif

#include "hlsl_clippy/control_flow.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::control_flow {

class CfgEngine {
public:
    /// Process-lifetime singleton. Lazily constructs on first call. The
    /// engine itself holds no Slang / tree-sitter session state -- those
    /// live with the parser and the reflection engine respectively -- so
    /// construction is cheap and re-entry-safe.
    [[nodiscard]] static CfgEngine& instance() noexcept;

    /// Build (or reuse) the CFG + uniformity oracle for `source`. When
    /// `reflection_or_null` is non-null, the uniformity analyzer can use
    /// its bindings to seed additional divergent sources (resource indices
    /// flagged NonUniform). Cache hits are O(log N) without re-walking the
    /// AST.
    [[nodiscard]] std::expected<ControlFlowInfo, Diagnostic> build(
        const SourceManager& sources,
        SourceId source,
        const ReflectionInfo* reflection_or_null,
        std::uint32_t cfg_inlining_depth);

    /// Drop every cached CFG. Intended for tests that want to force a
    /// fresh build between consecutive calls.
    void clear_cache();

    CfgEngine() = default;
    CfgEngine(const CfgEngine&) = delete;
    CfgEngine& operator=(const CfgEngine&) = delete;
    CfgEngine(CfgEngine&&) = delete;
    CfgEngine& operator=(CfgEngine&&) = delete;
    ~CfgEngine() = default;

    /// Pull the per-source diagnostics surfaced during the last build of
    /// `source`. The engine collects `clippy::cfg-skip` warnings from the
    /// builder and exposes them here so the orchestrator can append them
    /// to the lint output. Returns an empty vector when no diagnostics
    /// were recorded for the source.
    [[nodiscard]] std::vector<Diagnostic> take_diagnostics(SourceId source);

private:
    struct Entry {
        ControlFlowInfo info;
        std::vector<Diagnostic> diagnostics;
    };

    // Cache key includes a content fingerprint so two sources with the same
    // numeric `SourceId.value` (e.g. unit tests each constructing a fresh
    // SourceManager whose first add_buffer returns id `1`) but different
    // contents do not collide on the process-singleton cache.
    using CacheKey = std::pair<std::uint32_t, std::uint64_t>;
    mutable std::shared_mutex cache_mu_;
    detail::CfgCacheMap<CacheKey, std::shared_ptr<Entry>> cache_;
};

}  // namespace hlsl_clippy::control_flow
