// IR engine implementation -- sub-phase 7a.2 step 1 (ADR 0016).
//
// This file owns the public-side handshake between `lint()` and the IR
// pipeline: the singleton, the cache, the `analyze()` entry point.
//
// **Implementation status:** ADR 0016 §"4. Internal IrEngine" specifies
// that `analyze()` parses Slang's DXIL output via DXC's `DxilContainer`
// reader. That parser is the meat of 7a.2-step2 (DXC submodule + cmake +
// fetch scripts + LLVM bitcode reader); cross-platform DXC integration is
// genuinely a multi-day workstream and lands in a focused follow-up PR.
//
// 7a.2-step1 (this file) does the next-best thing without DXC: it reuses
// Slang's reflection result (already cached by the Phase 3
// `ReflectionEngine`) to populate `IrInfo` with one `IrFunction` per entry
// point, carrying the `entry_point_name` + `stage` + `declaration_span`.
// `IrFunction::blocks` stays empty -- per-instruction analysis is gated on
// 7a.2-step2. Rules that consume `find_function_by_name(...)` or iterate
// `IrInfo::functions` already work; rules that need block / instruction
// walks (the register-pressure, liveness, redundant-sample packs) will
// receive a separate not-implemented diagnostic when those packs come
// online and try to walk an empty block list.
//
// The orchestrator surfaces a single `clippy::ir-partial` Note per
// (source, profile) so users know the engine is metadata-only today.

#include "ir/engine.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "reflection/engine.hpp"
#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/ir.hpp"
#include "shader_clippy/reflection.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::ir {

namespace {

/// Build an `IrInfo` from the reflection result for one source. Per
/// 7a.2-step1, the only fields populated below the IrInfo level are
/// `IrFunction::entry_point_name`, `IrFunction::stage`, and
/// `IrFunction::declaration_span`. `IrFunction::blocks` stays empty; the
/// `IrFunctionId` value uses the entry point's index in the reflection
/// `entry_points` vector for deterministic identity (so two consecutive
/// `analyze()` calls on the same source assign the same id to the same
/// entry point).
[[nodiscard]] IrInfo build_ir_info_from_reflection(const ReflectionInfo& reflection) {
    IrInfo info;
    info.target_profile = reflection.target_profile;
    info.functions.reserve(reflection.entry_points.size());

    for (std::size_t i = 0; i < reflection.entry_points.size(); ++i) {
        const auto& ep = reflection.entry_points[i];
        IrFunction fn;
        // We can't construct `IrFunctionId(i)` directly because the
        // constructor is private. Default-constructed handles all carry
        // value 0; that's fine for 7a.2-step1 where rules look functions
        // up by name. 7a.2-step2's full builder will assign distinct ids
        // via the engine's friend access through `IrEngineFactory`.
        fn.entry_point_name = ep.name;
        fn.stage = ep.stage;
        fn.declaration_span = ep.declaration_span;
        info.functions.push_back(std::move(fn));
    }
    return info;
}

[[nodiscard]] std::uint64_t fingerprint(std::string_view bytes) noexcept {
    // FNV-1a 64-bit -- inexpensive content fingerprint for the cache key.
    // Matches the shape used by `ReflectionEngine`'s key. Cardinality of
    // the cache is bounded by sources-in-flight per lint run (typically
    // a handful), so collision risk at 2^64 is moot.
    constexpr std::uint64_t k_offset_basis = 0xCBF29CE484222325ULL;
    constexpr std::uint64_t k_prime = 0x100000001B3ULL;
    std::uint64_t h = k_offset_basis;
    for (const char c : bytes) {
        h ^= static_cast<std::uint8_t>(c);
        h *= k_prime;
    }
    return h;
}

}  // namespace

// PIMPL'd bridge placeholder. 7a.2-step1 doesn't construct one (`bridge_`
// stays null); 7a.2-step2 will replace this empty class with a real DXIL
// container reader + LLVM bitcode walker. The destructor is out-of-line so
// the `unique_ptr<DxilBridge>` member compiles under MSVC's stricter
// forward-decl rules.
class DxilBridge {};

IrEngine::IrEngine() = default;
IrEngine::~IrEngine() = default;

IrEngine& IrEngine::instance() noexcept {
    // Magic-static; thread-safe initialisation per C++11+ semantics.
    static IrEngine engine;
    return engine;
}

std::expected<IrInfo, Diagnostic> IrEngine::analyze(const SourceManager& sources,
                                                    SourceId source,
                                                    std::string_view target_profile) {
    // Cache lookup -- read lock first so concurrent lint runs on the same
    // process-singleton don't serialise.
    const std::string profile_key{target_profile};
    const auto* file = sources.get(source);
    if (file == nullptr) {
        Diagnostic diag;
        diag.code = std::string{"clippy::ir"};
        diag.severity = Severity::Note;
        diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
        diag.message = std::string{
            "IR analysis skipped: source id is not registered with the "
            "SourceManager (programmer error in the lint orchestrator)."};
        return std::unexpected(std::move(diag));
    }
    const auto fp = fingerprint(file->contents());
    const CacheKey key{source.value, profile_key, fp};

    {
        std::shared_lock lock{cache_mu_};
        const auto it = cache_.find(key);
        if (it != cache_.end()) {
            return *it->second;
        }
    }

    // Cache miss -- run reflection (or hit the reflection cache, which is
    // independent of this one). 7a.2-step1 piggybacks on reflection rather
    // than spinning up a second Slang compile path; once 7a.2-step2 lands
    // its DXC bridge, the additional `getEntryPointCode` call lives inside
    // the same Slang ISession invocation as reflection.
    auto& reflection_engine = reflection::ReflectionEngine::instance();
    auto reflection_or_error = reflection_engine.reflect(sources, source, target_profile);
    if (!reflection_or_error.has_value()) {
        // Reflection failed -- the orchestrator would have surfaced the
        // reflection error already if a Stage::Reflection rule was enabled.
        // We re-emit it under the IR code so the user sees IR was attempted.
        Diagnostic diag = std::move(reflection_or_error.error());
        diag.code = std::string{"clippy::ir"};
        return std::unexpected(std::move(diag));
    }

    auto ir_info = build_ir_info_from_reflection(reflection_or_error.value());
    auto shared = std::make_shared<const IrInfo>(std::move(ir_info));

    {
        std::unique_lock lock{cache_mu_};
        cache_[key] = shared;
    }

    return *shared;
}

void IrEngine::clear_cache() {
    std::unique_lock lock{cache_mu_};
    cache_.clear();
}

}  // namespace shader_clippy::ir
