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
#include <flat_map>
#include <memory>
#include <shared_mutex>

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

    mutable std::shared_mutex cache_mu_;
    std::flat_map<std::uint32_t, std::shared_ptr<Entry>> cache_;
};

}  // namespace hlsl_clippy::control_flow
