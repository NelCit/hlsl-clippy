// IR engine implementation -- sub-phase 7a.1 skeleton (ADR 0016).
//
// This file owns the public-side handshake between `lint()` and the IR
// pipeline: the singleton, the cache, the `analyze()` entry point. The DXC
// bridge that actually parses DXIL is deferred to sub-phase 7a.2; the entry
// point here always surfaces a single warn-severity
// `clippy::ir-not-implemented` diagnostic so the orchestrator can route IR-
// stage rules consistently and so unit tests can lock in the orchestrator
// behaviour today. When 7a.2 lands, only the body of `analyze()` changes
// (the diagnostic is replaced by a real bridge-driven parse path); the
// public API of this engine is stable.

#include "ir/engine.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/ir.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::ir {

namespace {

[[nodiscard]] Diagnostic make_not_implemented_diagnostic(SourceId source) {
    Diagnostic diag;
    diag.code = std::string{"clippy::ir-not-implemented"};
    diag.severity = Severity::Note;
    diag.primary_span = Span{.source = source, .bytes = ByteSpan{.lo = 0U, .hi = 0U}};
    diag.message = std::string{
        "IR-stage rules are not yet active in this build. Sub-phase 7a.1 of "
        "ADR 0016 (Phase 7 IR-level analysis infrastructure) lands the public "
        "API surface only; the DXC submodule + DXIL parser land in 7a.2. "
        "Once 7a.2 ships, IR-stage rules will fire automatically. AST + "
        "reflection + control-flow rules are unaffected."};
    return diag;
}

}  // namespace

// PIMPL'd bridge -- pointer is valid only after 7a.2 wires the
// `dxil_bridge.{hpp,cpp}` TU. Until then `bridge_` stays null and the entry
// point short-circuits to the not-implemented diagnostic. We still keep the
// destructor out-of-line so the unique_ptr to the incomplete `DxilBridge`
// type compiles under MSVC's stricter forward-decl rules.
class DxilBridge {};

IrEngine::IrEngine() = default;
IrEngine::~IrEngine() = default;

IrEngine& IrEngine::instance() noexcept {
    // Magic-static; thread-safe initialisation per C++11+ semantics.
    static IrEngine engine;
    return engine;
}

std::expected<IrInfo, Diagnostic> IrEngine::analyze(const SourceManager& /*sources*/,
                                                    SourceId source,
                                                    std::string_view /*target_profile*/) {
    // 7a.1: the bridge is not yet wired. Surface the not-implemented
    // diagnostic so the orchestrator routes Stage::Ir rules consistently
    // (single emission per source per lint run, anchored to byte 0..0,
    // severity Note so it doesn't drown out real lint output). The
    // diagnostic is engineering-facing -- it tells the user "this build
    // doesn't include IR-stage analysis yet" without scaring them about a
    // bug in their shader.
    return std::unexpected(make_not_implemented_diagnostic(source));
}

void IrEngine::clear_cache() {
    std::unique_lock lock{cache_mu_};
    cache_.clear();
}

}  // namespace hlsl_clippy::ir
