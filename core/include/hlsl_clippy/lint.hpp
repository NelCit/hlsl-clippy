#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy {

struct Config;

/// Run every rule against the source and return the accumulated diagnostics
/// in document order.
[[nodiscard]] std::vector<Diagnostic> lint(const SourceManager& sources,
                                           SourceId source,
                                           std::span<const std::unique_ptr<Rule>> rules);

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

/// Construct the default rule pack registered with v0.x. Phase 0 ships
/// `pow-const-squared` only.
[[nodiscard]] std::vector<std::unique_ptr<Rule>> make_default_rules();

}  // namespace hlsl_clippy
