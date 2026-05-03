// Internal reflection engine -- core-private API used by `lint.cpp`.
//
// The engine sits between the lint orchestrator and the Slang bridge. It owns
// the bridge as a PIMPL'd `SlangBridge`, holds a per-engine cache keyed by
// `(SourceId, target_profile)`, and exposes a single `reflect()` entry point.
// The engine is a process-singleton accessed via `instance()`; constructing a
// new engine per lint run would re-pay the IGlobalSession setup cost
// described in CLAUDE.md "Known issues to plan around".

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

// std::flat_map is C++23 (P0429) but landed in libstdc++ only at GCC 15.1
// and in libc++ only at libc++ 20. Ubuntu 24.04 (Linux CI) ships GCC 13's
// libstdc++ which lacks <flat_map>; ADR 0004 §"Selective C++26 adopt-now
// (feature-test gated)" pattern applies. Cache cardinality is small
// (typically < 100 entries per lint run) so std::map is fine here.
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
#include <flat_map>
namespace shader_clippy::reflection::detail {
template<typename K, typename V>
using ReflectionCacheMap = std::flat_map<K, V>;
}  // namespace shader_clippy::reflection::detail
#else
#include <map>
namespace shader_clippy::reflection::detail {
template<typename K, typename V>
using ReflectionCacheMap = std::map<K, V>;
}  // namespace shader_clippy::reflection::detail
#endif

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::reflection {

class SlangBridge;

class ReflectionEngine {
public:
    /// Process-lifetime singleton. Lazily constructs the underlying
    /// `SlangBridge` on first call (which lazily creates the `IGlobalSession`).
    /// Subsequent calls are O(1).
    [[nodiscard]] static ReflectionEngine& instance() noexcept;

    /// Compile + reflect `source` against `target_profile`. Cache hits on the
    /// `(source, target_profile)` tuple are O(log N) without re-compiling.
    /// Cache misses construct (or re-use) a pooled `ISession`, run Slang, and
    /// store a shared pointer to the result.
    [[nodiscard]] std::expected<ReflectionInfo, Diagnostic> reflect(
        const SourceManager& sources, SourceId source, std::string_view target_profile);

    /// Drop every cached `ReflectionInfo`. Intended for tests that want to
    /// force a fresh compile in the second of two consecutive calls.
    void clear_cache();

    explicit ReflectionEngine(std::uint32_t pool_size);
    ReflectionEngine(const ReflectionEngine&) = delete;
    ReflectionEngine& operator=(const ReflectionEngine&) = delete;
    ReflectionEngine(ReflectionEngine&&) = delete;
    ReflectionEngine& operator=(ReflectionEngine&&) = delete;
    ~ReflectionEngine();

private:
    // Cache key includes a content fingerprint so that two sources with the
    // same numeric `SourceId.value` (e.g. two unit tests each constructing a
    // fresh SourceManager whose first add_buffer returns id `1`) but
    // different contents do not collide on the process-singleton cache.
    using CacheKey = std::tuple<std::uint32_t, std::string, std::uint64_t>;

    std::unique_ptr<SlangBridge> bridge_;
    mutable std::shared_mutex cache_mu_;
    detail::ReflectionCacheMap<CacheKey, std::shared_ptr<const ReflectionInfo>> cache_;
};

}  // namespace shader_clippy::reflection
