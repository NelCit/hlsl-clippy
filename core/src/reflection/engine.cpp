#include "reflection/engine.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/reflection.hpp"
#include "hlsl_clippy/source.hpp"
#include "reflection/slang_bridge.hpp"

namespace hlsl_clippy::reflection {

namespace {

/// FNV-1a 64-bit hash of the source contents. Used to disambiguate cache
/// entries whose `SourceId.value` collides across SourceManagers (e.g., unit
/// tests that each construct a fresh SourceManager whose first add_buffer
/// returns id 1).
[[nodiscard]] std::uint64_t fingerprint(std::string_view contents) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    constexpr std::uint64_t k_prime = 1099511628211ULL;
    for (const char c : contents) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= k_prime;
    }
    return hash;
}

}  // namespace

ReflectionEngine::ReflectionEngine(std::uint32_t pool_size)
    : bridge_(std::make_unique<SlangBridge>(pool_size)) {}

ReflectionEngine::~ReflectionEngine() = default;

ReflectionEngine& ReflectionEngine::instance() noexcept {
    // Meyers singleton: the Slang IGlobalSession is documented as expensive
    // to construct, so keep one for the process lifetime. Default pool size
    // matches `LintOptions::reflection_pool_size`.
    static ReflectionEngine engine{4U};
    return engine;
}

std::expected<ReflectionInfo, Diagnostic> ReflectionEngine::reflect(
    const SourceManager& sources, SourceId source, std::string_view target_profile) {
    const SourceFile* file = sources.get(source);
    const std::uint64_t fp = file != nullptr ? fingerprint(file->contents()) : 0ULL;
    const CacheKey key{source.value, std::string{target_profile}, fp};

    {
        std::shared_lock<std::shared_mutex> read_lock(cache_mu_);
        const auto it = cache_.find(key);
        if (it != cache_.end()) {
            // Copy out of the shared_ptr's payload. ReflectionInfo is a flat
            // value type so the copy is cheap relative to compiling Slang.
            return *it->second;
        }
    }

    auto computed = bridge_->reflect(sources, source, target_profile);
    if (!computed.has_value()) {
        return std::unexpected{std::move(computed.error())};
    }

    auto stored = std::make_shared<const ReflectionInfo>(std::move(computed.value()));
    {
        std::unique_lock<std::shared_mutex> write_lock(cache_mu_);
        // Tolerate a concurrent insert: try_emplace returns the existing
        // entry if another caller raced us between the read-unlock and
        // write-lock; either way, *it->second is the canonical value.
        const auto result = cache_.try_emplace(key, stored);
        return *result.first->second;
    }
}

void ReflectionEngine::clear_cache() {
    std::unique_lock<std::shared_mutex> write_lock(cache_mu_);
    cache_.clear();
}

}  // namespace hlsl_clippy::reflection
