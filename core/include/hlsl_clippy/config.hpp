#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"

namespace hlsl_clippy {

/// Per-rule severity dial selectable from `.hlsl-clippy.toml`. The mapping to
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

/// One `[[overrides]]` entry.
struct RuleOverride {
    /// Glob expression matched against the file path being linted. Supports
    /// `**` (any number of path components, including zero), `*` (anything
    /// except a path separator), and `?` (a single character).
    std::string path_glob;
    /// Per-rule severity overrides applied when the file path matches.
    std::unordered_map<std::string, RuleSeverity> rule_severity;
};

/// Parsed `.hlsl-clippy.toml`. Constructed via `load_config`.
struct Config {
    /// Top-level `[rules]` table.
    std::unordered_map<std::string, RuleSeverity> rule_severity;
    /// `[includes] patterns = [...]`.
    std::vector<std::string> includes;
    /// `[excludes] patterns = [...]`.
    std::vector<std::string> excludes;
    /// `[[overrides]]` entries, in source order (later wins).
    std::vector<RuleOverride> overrides;

    /// Resolve the effective severity for `rule_id` when linting `file_path`.
    /// Returns `std::nullopt` when the config doesn't mention the rule (the
    /// caller should fall back to the rule's built-in severity).
    [[nodiscard]] std::optional<RuleSeverity> severity_for(
        std::string_view rule_id,
        const std::filesystem::path& file_path) const;
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

/// Load `.hlsl-clippy.toml` from disk.
[[nodiscard]] ConfigResult load_config(const std::filesystem::path& path);

/// Parse a TOML config from an in-memory string. The optional `origin`
/// parameter is reflected back through the error path on failure but is not
/// otherwise consulted.
[[nodiscard]] ConfigResult load_config_string(std::string_view contents,
                                              const std::filesystem::path& origin = {});

/// Walk parents of `start` looking for `.hlsl-clippy.toml`. Stops at:
/// 1. The first parent that contains a `.hlsl-clippy.toml`, returning that path.
/// 2. The first parent that contains a `.git/` entry (workspace boundary):
///    no further search is performed and `std::nullopt` is returned.
/// 3. The filesystem root.
///
/// `start` may point at either a file or a directory. If it's a file, the
/// search begins in the file's parent.
[[nodiscard]] std::optional<std::filesystem::path> find_config(
    const std::filesystem::path& start);

/// True if `path` matches `glob`. Supports `**` (any number of path
/// components, including zero), `*` (anything except a path separator), and
/// `?` (a single character). Comparison is case-sensitive on POSIX and
/// case-insensitive on Windows for letters; separators are normalised to `/`
/// before matching.
[[nodiscard]] bool path_glob_match(std::string_view glob, const std::filesystem::path& path);

}  // namespace hlsl_clippy
