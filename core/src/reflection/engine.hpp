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
#include <flat_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::reflection {

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
        const SourceManager& sources,
        SourceId source,
        std::string_view target_profile);

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
    using CacheKey = std::pair<std::uint32_t, std::string>;

    std::unique_ptr<SlangBridge> bridge_;
    mutable std::shared_mutex cache_mu_;
    std::flat_map<CacheKey, std::shared_ptr<const ReflectionInfo>> cache_;
};

}  // namespace hlsl_clippy::reflection
