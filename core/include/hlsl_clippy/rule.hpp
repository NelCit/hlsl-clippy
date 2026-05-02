#pragma once

#include <cstdint>
#include <string_view>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy {

class SuppressionSet;
struct Config;

/// Experimental IHV target gate selectable from `.hlsl-clippy.toml` via
/// `[experimental] target = "rdna4" | "blackwell" | "xe2"`. Per ADR 0018,
/// vendor-specific rules ship behind this gate so default builds emit zero
/// IHV-specific diagnostics. Rules opt in by overriding
/// `Rule::experimental_target()`; the orchestrator skips them silently when
/// the rule's target does not match `Config::experimental_target()`.
///
/// The enum lives here (rather than in `hlsl_clippy/config.hpp`) so that
/// rule TUs can reference `ExperimentalTarget::Rdna4` without pulling in
/// the heavier config header (`<filesystem>` / `<unordered_map>` /
/// `<variant>`). `hlsl_clippy/config.hpp` includes this header to get the
/// enum back.
enum class ExperimentalTarget : std::uint8_t {
    /// Default — no experimental target selected. Every rule whose
    /// `experimental_target()` is non-`None` is skipped.
    None,
    /// AMD RDNA 4 (Radeon RX 9070 / 9070 XT, Feb 2025). Gates rules anchored
    /// on dynamic-VGPR mode, the read/write coalescing buffer, and the
    /// 2nd-gen AI accelerator's FP8 cooperative-matrix layouts.
    Rdna4,
    /// NVIDIA Blackwell / RTX 50 (early 2025). Gates rules anchored on
    /// FP4 / FP6 cooperative matrix layouts that differ from Hopper FP8.
    Blackwell,
    /// Intel Xe2 / Battlemage (B580, late 2024). Gates rules anchored on
    /// SIMD16 native execution and the dispatch-shape interactions with
    /// `[WaveSize(32)]` declarations.
    Xe2,
};

/// Pipeline stage at which a rule's hook fires. Phase 0/1/2 ships `Ast`;
/// Phase 3 (ADR 0012) introduces `Reflection` for rules that need Slang
/// reflection data (resource bindings, cbuffer layouts, entry-point shape);
/// Phase 4 (ADR 0013) introduces `ControlFlow` for rules that need the
/// per-source CFG + uniformity oracle; Phase 7 (ADR 0016) introduces `Ir`
/// for rules that need a post-codegen DXIL view (liveness, register
/// pressure, DXR / SM 6.9 / mesh-shader intrinsics).
enum class Stage {
    Ast,          ///< AST-only (default; Phase 0/1/2).
    Reflection,   ///< Needs `ReflectionInfo` (Phase 3).
    ControlFlow,  ///< Needs `ControlFlowInfo` (Phase 4).
    Ir,           ///< Needs `IrInfo` (Phase 7 -- ADR 0016).
};

/// Forward declaration of the tree-sitter node wrapper. The full definition
/// lives in `core/src/parser_internal.hpp` and is not visible to public-header
/// consumers (CLI, future LSP). Rules cast through this opaque wrapper.
class AstCursor;

/// Forward declaration of the tree-sitter tree wrapper. Used by rules that
/// drive a TSQuery match loop (the declarative path).
class AstTree;

/// Forward declaration of the public reflection aggregate. The full definition
/// lives in `hlsl_clippy/reflection.hpp`; `Rule::on_reflection` takes a const
/// reference so this header does not need to pull the (heavier) reflection
/// header into every rule TU.
struct ReflectionInfo;

/// Forward declaration of the public control-flow aggregate. The full
/// definition lives in `hlsl_clippy/control_flow.hpp`; `Rule::on_cfg` takes a
/// const reference so this header does not need to pull the (heavier)
/// control-flow header into every rule TU.
struct ControlFlowInfo;

/// Forward declaration of the public IR aggregate. The full definition lives
/// in `hlsl_clippy/ir.hpp`; `Rule::on_ir` takes a const reference so this
/// header does not need to pull the (heavier) IR header into every rule TU.
struct IrInfo;

/// Context passed to rule hooks. Owns the diagnostic sink for the in-progress
/// lint pass and exposes the source under analysis.
class RuleContext {
public:
    RuleContext(const SourceManager& sources, SourceId source) noexcept
        : sources_(&sources), source_(source) {}

    [[nodiscard]] const SourceManager& sources() const noexcept {
        return *sources_;
    }
    [[nodiscard]] SourceId source() const noexcept {
        return source_;
    }

    /// Install a suppression filter. Diagnostics emitted via `emit()` whose
    /// span intersects an active suppression are dropped silently. The pointer
    /// is borrowed and must outlive the `RuleContext`.
    void set_suppressions(const SuppressionSet* suppressions) noexcept {
        suppressions_ = suppressions;
    }

    /// Install a (borrowed) config pointer. Rules that consume tunable knobs
    /// (`compare_epsilon()`, `div_epsilon()`, future per-rule scalar dials)
    /// read from `config()` and tolerate a `nullptr` return. The pointer is
    /// borrowed and must outlive the `RuleContext`. The orchestrator wires
    /// this in the config-aware `lint(..., const Config&, ...)` overload;
    /// non-config-aware overloads leave it null and rules fall back to
    /// hard-coded defaults (ADR 0019 §"v1.x patch trajectory").
    void set_config(const Config* config) noexcept {
        config_ = config;
    }

    /// Active configuration, or `nullptr` when the lint run was started via a
    /// non-config-aware overload. Rules MUST tolerate the null case (treat it
    /// as "use defaults").
    [[nodiscard]] const Config* config() const noexcept {
        return config_;
    }

    /// Append a diagnostic to the current pass. Diagnostics matching an
    /// active inline-suppression are dropped silently.
    void emit(Diagnostic diag);

    /// Steal the accumulated diagnostics. Called by the lint orchestrator.
    [[nodiscard]] std::vector<Diagnostic> take_diagnostics() noexcept;

private:
    const SourceManager* sources_;
    SourceId source_;
    const SuppressionSet* suppressions_ = nullptr;
    const Config* config_ = nullptr;
    std::vector<Diagnostic> diagnostics_;
};

/// Abstract interface every rule implements.
///
/// Phase 0 keeps the surface minimal: each rule announces its identity and
/// implements `on_node` for the syntactic match. Phase 1 will layer
/// declarative tree-sitter queries on top of this interface.
class Rule {
public:
    Rule() = default;
    Rule(const Rule&) = delete;
    Rule& operator=(const Rule&) = delete;
    Rule(Rule&&) = delete;
    Rule& operator=(Rule&&) = delete;
    virtual ~Rule() = default;

    /// Stable identifier, e.g. `"pow-const-squared"`. Matches the diagnostic
    /// code emitted by the rule.
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /// Category tag, e.g. `"math"`. Used by the rule catalog and config.
    [[nodiscard]] virtual std::string_view category() const noexcept = 0;

    /// Stage at which the rule fires.
    [[nodiscard]] virtual Stage stage() const noexcept {
        return Stage::Ast;
    }

    /// Override to mark a rule as gated behind `[experimental.target = X]`.
    /// Default `ExperimentalTarget::None` keeps the rule always-on (subject
    /// to the usual rule-selection / severity dials). When the override
    /// returns a non-`None` target, the orchestrator only invokes the rule
    /// when `Config::experimental_target()` matches; otherwise it is
    /// skipped silently with no diagnostic so default builds remain free
    /// of IHV-specific noise (ADR 0018).
    [[nodiscard]] virtual ExperimentalTarget experimental_target() const noexcept {
        return ExperimentalTarget::None;
    }

    /// Visit one AST node. Called by the lint orchestrator for every named
    /// node in document order. Default implementation does nothing.
    virtual void on_node(const AstCursor& cursor, RuleContext& ctx);

    /// Whole-tree hook called once per parsed source. Declarative rules use
    /// this to drive a TSQuery match loop in one shot rather than walking
    /// every named node imperatively. Default implementation does nothing.
    virtual void on_tree(const AstTree& tree, RuleContext& ctx);

    /// Reflection-stage hook called once per parsed source for rules with
    /// `stage() == Stage::Reflection` (ADR 0012). The orchestrator runs the
    /// reflection engine at most once per `(SourceId, target_profile)` tuple
    /// per lint run and dispatches the cached `ReflectionInfo` to every
    /// reflection-stage rule. Rules retain access to the `AstTree` because
    /// most of them want both sides: reflection answers "what" the resource
    /// is, the AST answers "where" in source it was used. Default
    /// implementation does nothing.
    virtual void on_reflection(const AstTree& tree,
                               const ReflectionInfo& reflection,
                               RuleContext& ctx);

    /// Control-flow-stage hook called once per parsed source for rules with
    /// `stage() == Stage::ControlFlow` (ADR 0013). The orchestrator builds
    /// the per-source CFG + uniformity oracle at most once per lint run and
    /// dispatches the cached `ControlFlowInfo` to every control-flow-stage
    /// rule. Rules retain access to the `AstTree` because most of them want
    /// both sides: the CFG tells them "what path", the AST tells them "what
    /// syntactic shape". Default implementation does nothing.
    virtual void on_cfg(const AstTree& tree, const ControlFlowInfo& cfg, RuleContext& ctx);

    /// IR-stage hook called once per parsed source per target profile for
    /// rules with `stage() == Stage::Ir` (ADR 0016). The orchestrator runs
    /// the IR engine at most once per `(SourceId, target_profile)` tuple per
    /// lint run -- it reuses the existing Slang `ISession` pool from ADR
    /// 0012 and additionally captures the DXIL blob that Slang emits, then
    /// parses the blob into an `IrInfo`. Rules retain access to the
    /// `AstTree` because diagnostic anchoring sometimes prefers the source
    /// declaration over an IR debug-info span (e.g. `oversized-ray-payload`
    /// anchors to the payload `struct` declaration in source, not the
    /// `OpStore` in IR). Default implementation does nothing.
    virtual void on_ir(const AstTree& tree, const IrInfo& ir, RuleContext& ctx);
};

}  // namespace hlsl_clippy
