#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy {

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

}  // namespace hlsl_clippy
