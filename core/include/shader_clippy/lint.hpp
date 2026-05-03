#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/rule.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy {

struct Config;

/// Runtime knobs for the lint pipeline (per ADR 0012). All fields default to
/// values that preserve the existing AST-only behaviour, so a default-
/// constructed `LintOptions{}` is equivalent to the historical single-arg-
/// rules `lint()` call.
struct LintOptions {
    /// Override the default target profile used for reflection. When empty,
    /// the engine picks per-stage defaults (vs_6_6 / ps_6_6 / cs_6_6 / ...).
    /// Ignored entirely when no enabled rule has `stage() == Stage::Reflection`.
    std::optional<std::string> target_profile;

    /// When false, the reflection stage is skipped even if reflection-stage
    /// rules are enabled. Useful for fast iteration / AST-only smoke runs /
    /// tests that want to isolate AST behaviour from reflection.
    bool enable_reflection = true;

    /// Pool size for `ISession` workers in the reflection engine. Default 4;
    /// LSP / batch CI may want more. Ignored when reflection is not invoked.
    std::uint32_t reflection_pool_size = 4;

    /// When false, the control-flow stage is skipped entirely even if
    /// control-flow-stage rules are enabled (ADR 0013). Useful for fast
    /// iteration / AST-only smoke runs / tests that want to isolate AST and
    /// reflection behaviour from the CFG engine. Ignored when no enabled
    /// rule has `stage() == Stage::ControlFlow`.
    bool enable_control_flow = true;

    /// Maximum inter-procedural inlining depth for the uniformity analyzer
    /// (ADR 0013). Default 3. Higher values produce more precise uniformity
    /// facts at higher build-time cost; rules that need deeper reasoning are
    /// explicitly Phase 7. Ignored when CFG construction is not invoked.
    std::uint32_t cfg_inlining_depth = 3;

    /// When false, the IR stage is skipped entirely even if IR-stage rules
    /// are enabled (ADR 0016). Useful for CI runs that want to isolate
    /// Phase 7 cost, or for downstream consumers built with
    /// `SHADER_CLIPPY_ENABLE_IR=OFF` where the engine simply isn't linked in.
    /// Ignored when no enabled rule has `stage() == Stage::Ir`.
    bool enable_ir = true;

    /// Per-instruction live-value count above which `vgpr-pressure-warning`
    /// fires (ADR 0016 §"Shared utilities"). Default 64 (~RDNA wave32 ×2).
    /// Per-arch refinement is a v0.8+ follow-up. Ignored when no enabled
    /// rule consumes the register-pressure estimator.
    std::uint32_t vgpr_pressure_threshold = 64;
};

/// Run every rule against the source and return the accumulated diagnostics
/// in document order. Equivalent to the four-arg overload with
/// `LintOptions{}`.
[[nodiscard]] std::vector<Diagnostic> lint(const SourceManager& sources,
                                           SourceId source,
                                           std::span<const std::unique_ptr<Rule>> rules);

/// Reflection-aware overload (ADR 0012). The pipeline still runs every rule's
/// `Stage::Ast` hooks first; if any enabled rule has
/// `stage() == Stage::Reflection` AND `options.enable_reflection` is true,
/// the orchestrator invokes the reflection engine once per source per profile,
/// caches the resulting `ReflectionInfo`, and dispatches every reflection-
/// stage rule's `on_reflection` hook against it. AST-only rule packs pay zero
/// Slang cost: the engine is never constructed when no reflection-stage rule
/// is enabled.
[[nodiscard]] std::vector<Diagnostic> lint(const SourceManager& sources,
                                           SourceId source,
                                           std::span<const std::unique_ptr<Rule>> rules,
                                           const LintOptions& options);

/// Config-aware overload. `config` selects which rules run and what severity
/// each diagnostic gets:
///
/// - Rules with severity `Allow` are skipped entirely (never invoked).
/// - Rules with severity `Warn` re-tag every emitted diagnostic at
///   `Severity::Warning`.
/// - Rules with severity `Deny` re-tag every emitted diagnostic at
///   `Severity::Error`.
///
/// `file_path` is matched against `[[overrides]]` globs. Pass an empty path
/// to disable override matching.
[[nodiscard]] std::vector<Diagnostic> lint(const SourceManager& sources,
                                           SourceId source,
                                           std::span<const std::unique_ptr<Rule>> rules,
                                           const Config& config,
                                           const std::filesystem::path& file_path);

/// Config + LintOptions overload. Combines `Config`-driven rule selection /
/// severity re-tagging (above) with the reflection knobs from `LintOptions`.
[[nodiscard]] std::vector<Diagnostic> lint(const SourceManager& sources,
                                           SourceId source,
                                           std::span<const std::unique_ptr<Rule>> rules,
                                           const Config& config,
                                           const std::filesystem::path& file_path,
                                           const LintOptions& options);

/// Construct the default rule pack registered with v0.x. Phase 0 ships
/// `pow-const-squared` only.
[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_default_rules();

}  // namespace shader_clippy
