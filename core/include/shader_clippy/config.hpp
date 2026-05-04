#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/language.hpp"
#include "shader_clippy/rule.hpp"

namespace shader_clippy {

/// Per-rule severity dial selectable from `.shader-clippy.toml`. The mapping to
/// the diagnostic `Severity` enum is:
///
/// - `Allow` — drop every diagnostic for this rule (the rule does not even
///   run; equivalent to a project-wide suppression).
/// - `Warn`  — emit at `Severity::Warning`.
/// - `Deny`  — emit at `Severity::Error`.
///
/// The default for a rule with no entry in the config is its built-in
/// severity (typically `Warning`).
enum class RuleSeverity {
    Allow,
    Warn,
    Deny,
};

// `ExperimentalTarget` is declared in `shader_clippy/rule.hpp` (included
// above) so that rule TUs can reference its enumerators without pulling in
// the heavier config header.

/// One `[[overrides]]` entry.
struct RuleOverride {
    /// Glob expression matched against the file path being linted. Supports
    /// `**` (any number of path components, including zero), `*` (anything
    /// except a path separator), and `?` (a single character).
    std::string path_glob;
    /// Per-rule severity overrides applied when the file path matches.
    std::unordered_map<std::string, RuleSeverity> rule_severity;
};

/// Default value for `Config::compare_epsilon()`. Matches the historical
/// hard-coded value used by `compare-equal-float` and friends.
inline constexpr float k_default_compare_epsilon = 0.0001F;

/// Default value for `Config::div_epsilon()`. Matches the historical
/// hard-coded value used by `div-without-epsilon` and friends.
inline constexpr float k_default_div_epsilon = 1.0e-6F;

/// Parsed `.shader-clippy.toml`. Constructed via `load_config`.
struct Config {
    /// Top-level `[rules]` table.
    std::unordered_map<std::string, RuleSeverity> rule_severity;
    /// `[includes] patterns = [...]`.
    std::vector<std::string> includes;
    /// `[excludes] patterns = [...]`.
    std::vector<std::string> excludes;
    /// `[shader] include-directories = [...]`. Relative entries loaded from
    /// disk are resolved against the `.shader-clippy.toml` directory.
    std::vector<std::filesystem::path> shader_include_directories;
    /// `[[overrides]]` entries, in source order (later wins).
    std::vector<RuleOverride> overrides;
    /// `[experimental] target = "rdna4" | "blackwell" | "xe2"`. Default
    /// `None`. Unrecognised values fall back to `None` and surface a
    /// human-readable string in `warnings`.
    ExperimentalTarget experimental_target_value = ExperimentalTarget::None;
    /// `[float] compare-epsilon`. Tunes the noise floor that
    /// numerical-comparison rules treat as "effectively equal" — see
    /// `compare-equal-float`, `comparison-with-nan-literal`, and other
    /// float-comparison rules. Default `k_default_compare_epsilon`.
    /// Out-of-range / non-numeric values fall back to the default and
    /// surface a `warnings` entry.
    float compare_epsilon_value = k_default_compare_epsilon;
    /// `[float] div-epsilon`. Tunes the threshold below which the
    /// `div-without-epsilon` rule (and successor rules) consider a
    /// divisor "potentially zero". Default `k_default_div_epsilon`.
    /// Out-of-range / non-numeric values fall back to the default and
    /// surface a `warnings` entry.
    float div_epsilon_value = k_default_div_epsilon;
    /// `[lint] source-language` (ADR 0020 sub-phase A — v1.3.0). Selects
    /// which frontend the orchestrator engages: `Auto` (default) infers
    /// from the file extension, `Hlsl` forces tree-sitter-hlsl + Slang
    /// HLSL frontend, `Slang` skips AST + CFG dispatch and runs only the
    /// Reflection-stage rules through Slang's native Slang frontend.
    /// Out-of-range / non-string values fall back to `Auto` and surface a
    /// `warnings` entry.
    SourceLanguage source_language_value = SourceLanguage::Auto;
    /// Soft warnings collected while parsing the config. Each entry is a
    /// human-readable single-line message; the driver renders them as
    /// `clippy::config` `Severity::Warning` diagnostics. Hard parse errors
    /// surface via `ConfigError` instead and never reach this vector.
    std::vector<std::string> warnings;

    /// Resolve the effective severity for `rule_id` when linting `file_path`.
    /// Returns `std::nullopt` when the config doesn't mention the rule (the
    /// caller should fall back to the rule's built-in severity).
    [[nodiscard]] std::optional<RuleSeverity> severity_for(
        std::string_view rule_id, const std::filesystem::path& file_path) const;

    /// Selected `[experimental] target`. Default `ExperimentalTarget::None`.
    /// The orchestrator uses this to filter rules with a non-`None`
    /// `Rule::experimental_target()`.
    [[nodiscard]] ExperimentalTarget experimental_target() const noexcept {
        return experimental_target_value;
    }

    /// Configured `[float] compare-epsilon`. Default
    /// `k_default_compare_epsilon` (0.0001).
    [[nodiscard]] float compare_epsilon() const noexcept {
        return compare_epsilon_value;
    }

    /// Configured `[float] div-epsilon`. Default
    /// `k_default_div_epsilon` (1e-6).
    [[nodiscard]] float div_epsilon() const noexcept {
        return div_epsilon_value;
    }

    /// Configured `[lint] source-language` (ADR 0020 sub-phase A — v1.3.0).
    /// Default `SourceLanguage::Auto`. The orchestrator passes this through
    /// `resolve_language()` together with the file path to decide whether
    /// AST + CFG rules dispatch (HLSL) or skip with a one-shot
    /// `clippy::language-skip-ast` notice (Slang).
    [[nodiscard]] SourceLanguage source_language() const noexcept {
        return source_language_value;
    }
};

/// Configuration loader error. Carries enough context to render a
/// `clippy::config` diagnostic without leaking toml++ types into the public
/// header surface.
struct ConfigError {
    /// Human-readable description of the error.
    std::string message;
    /// Path to the config file that produced the error. May be empty when the
    /// error originates from a synthetic in-memory string.
    std::filesystem::path source;
    /// 1-based line and column of the offending token. Zero means "unknown".
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

/// Result of `load_config` / `load_config_string`. Either holds a parsed
/// `Config` or a `ConfigError` describing what went wrong.
class ConfigResult {
public:
    ConfigResult(Config cfg) noexcept : payload_(std::move(cfg)) {}
    ConfigResult(ConfigError err) noexcept : payload_(std::move(err)) {}

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<Config>(payload_);
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] const Config& value() const& {
        return std::get<Config>(payload_);
    }
    [[nodiscard]] Config&& value() && {
        return std::get<Config>(std::move(payload_));
    }

    [[nodiscard]] const ConfigError& error() const& {
        return std::get<ConfigError>(payload_);
    }

private:
    std::variant<Config, ConfigError> payload_;
};

/// Load `.shader-clippy.toml` from disk.
[[nodiscard]] ConfigResult load_config(const std::filesystem::path& path);

/// Parse a TOML config from an in-memory string. The optional `origin`
/// parameter is reflected back through the error path on failure but is not
/// otherwise consulted.
[[nodiscard]] ConfigResult load_config_string(std::string_view contents,
                                              const std::filesystem::path& origin = {});

/// Walk parents of `start` looking for `.shader-clippy.toml`. Stops at:
/// 1. The first parent that contains a `.shader-clippy.toml`, returning that path.
/// 2. The first parent that contains a `.git/` entry (workspace boundary):
///    no further search is performed and `std::nullopt` is returned.
/// 3. The filesystem root.
///
/// `start` may point at either a file or a directory. If it's a file, the
/// search begins in the file's parent.
[[nodiscard]] std::optional<std::filesystem::path> find_config(const std::filesystem::path& start);

/// True if `path` matches `glob`. Supports `**` (any number of path
/// components, including zero), `*` (anything except a path separator), and
/// `?` (a single character). Comparison is case-sensitive on POSIX and
/// case-insensitive on Windows for letters; separators are normalised to `/`
/// before matching.
[[nodiscard]] bool path_glob_match(std::string_view glob, const std::filesystem::path& path);

}  // namespace shader_clippy
