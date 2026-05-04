// Internal IR engine -- core-private API used by `lint.cpp`.
//
// The engine sits between the lint orchestrator and the (eventual) DXC bridge.
// Per ADR 0016, it owns the bridge as a PIMPL'd `DxilBridge`, holds a
// per-engine cache keyed by `(SourceId, target_profile)`, and exposes a
// single `analyze()` entry point. The engine is a process-singleton accessed
// via `instance()`; constructing a new engine per lint run would re-pay the
// DXC initialisation cost (parallel to the Slang `IGlobalSession` cost
// described in CLAUDE.md "Known issues to plan around").
//
// **Sub-phase 7a.1 status (this file):** the engine is a SKELETON. It
// surfaces the public-side handshake (`Stage::Ir` rules + orchestrator
// dispatch + `LintOptions::enable_ir` gating) but the bridge is not yet
// wired -- `analyze()` always returns a single warn-severity
// `clippy::ir-not-implemented` diagnostic. Sub-phase 7a.2 lands the DXC
// submodule, `cmake/UseDxc.cmake`, and the real `dxil_bridge.cpp` parser.
// Once 7a.2 lands, only `analyze()`'s body changes; the public surface here
// is stable.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

// std::flat_map is C++23 (P0429) but landed in libstdc++ only at GCC 15.1
// and in libc++ only at libc++ 20. Mirror the same fallback as the Phase 3
// reflection engine (cache cardinality is small; std::map is fine).
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
#include <flat_map>
namespace shader_clippy::ir::detail {
template<typename K, typename V>
using IrCacheMap = std::flat_map<K, V>;
}  // namespace shader_clippy::ir::detail
#else
#include <map>
namespace shader_clippy::ir::detail {
template<typename K, typename V>
using IrCacheMap = std::map<K, V>;
}  // namespace shader_clippy::ir::detail
#endif

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/ir.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::ir {

class DxilBridge;  // forward decl; defined in dxil_bridge.hpp once 7a.2 lands.

class IrEngine {
public:
    /// Process-lifetime singleton. Lazily constructs the underlying DXC
    /// bridge on first call (which lazily creates the DXC IDxcUtils +
    /// IDxcCompiler3 instances). Subsequent calls are O(1).
    [[nodiscard]] static IrEngine& instance() noexcept;

    /// Compile + parse `source` against `target_profile` into an
    /// `IrInfo`. Cache hits on the `(source, target_profile)` tuple are
    /// O(log N) without re-parsing. Cache misses construct (or re-use)
    /// the DXC bridge, request a DXIL blob from Slang, parse the
    /// `DxilContainer`, project debug-info onto `(SourceId, ByteSpan)`,
    /// and store a shared pointer to the result.
    ///
    /// **7a.1 stub behaviour:** always returns the
    /// `clippy::ir-not-implemented` warn diagnostic (the DXC bridge is
    /// not yet wired). The orchestrator surfaces the diagnostic once per
    /// source per lint run and skips IR-stage rule dispatch for that
    /// source. AST / reflection / control-flow rules continue to fire.
    [[nodiscard]] std::expected<IrInfo, Diagnostic> analyze(
        const SourceManager& sources,
        SourceId source,
        std::string_view target_profile,
        std::span<const std::filesystem::path> include_directories = {});

    /// Drop every cached `IrInfo`. Intended for tests that want to force
    /// a fresh parse on the second of two consecutive calls.
    void clear_cache();

    explicit IrEngine();
    IrEngine(const IrEngine&) = delete;
    IrEngine& operator=(const IrEngine&) = delete;
    IrEngine(IrEngine&&) = delete;
    IrEngine& operator=(IrEngine&&) = delete;
    ~IrEngine();

private:
    // Cache key matches ADR 0012's `(SourceId.value, target_profile,
    // content_fingerprint)` shape so two unit tests each constructing a
    // fresh `SourceManager` whose first `add_buffer` returns id 1 don't
    // collide on the process-singleton cache.
    using CacheKey = std::tuple<std::uint32_t, std::string, std::uint64_t, std::string>;

    std::unique_ptr<DxilBridge> bridge_;
    mutable std::shared_mutex cache_mu_;
    detail::IrCacheMap<CacheKey, std::shared_ptr<const IrInfo>> cache_;
};

}  // namespace shader_clippy::ir
