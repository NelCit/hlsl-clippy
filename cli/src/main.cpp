#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/language.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rewriter.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/version.hpp"

namespace {

// NOLINTBEGIN(readability-identifier-naming)
// CamelCase enum-class members match the rest of the codebase
// (Severity::Error/Warning/Note, Stage::Ast, etc.) per CLAUDE.md
// "Code standards — Naming conventions" — `CamelCase` for enums.
// clang-tidy's k_-prefix-on-constant rule misfires on enum-class members.
enum class OutputFormat : std::uint8_t {
    Human,              ///< rustc-style spans + caret line. Default for TTY.
    Json,               ///< Flat array of diagnostic objects. Stable schema for CI.
    GithubAnnotations,  ///< `::warning file=...,line=...::msg [rule]` workflow commands.
};
// NOLINTEND(readability-identifier-naming)

// File extensions the CLI recognises by default. The list is informational
// today (positional path arguments accept any extension verbatim — no
// whitelist filtering happens) and is consumed by `--source-language=auto`
// inference inside `core/src/language.cpp`. Adding `.slang` here documents
// the v1.3 surface (ADR 0020 sub-phase A) and pre-positions the list for
// future glob-walking work (e.g. `hlsl-clippy lint --recursive shaders/`).
[[maybe_unused]] constexpr std::array<std::string_view, 11> k_recognized_extensions = {
    ".hlsl",
    ".hlsli",
    ".fx",
    ".fxh",
    ".vsh",
    ".psh",
    ".csh",
    ".gsh",
    ".hsh",
    ".dsh",
    ".slang",
};

void print_usage() {
    std::cout << "hlsl-clippy " << hlsl_clippy::version() << "\n"
              << "Usage: hlsl-clippy <command> [args]\n"
              << "\n"
              << "Commands:\n"
              << "  lint <file>...  [--fix] [--config <path>] [--target-profile <p>]\n"
              << "                  [--format=<human|json|github-annotations>]\n"
              << "                  [--source-language=<auto|hlsl|slang>]\n"
              << "                Lint one or more HLSL or Slang source files.\n"
              << "                Multi-file\n"
              << "                invocation amortizes Slang init / rule registry\n"
              << "                / reflection cache across all files in one process\n"
              << "                (3-10x speedup vs N separate invocations on a\n"
              << "                shader tree). With --fix, apply\n"
              << "                machine-applicable rewrites in place. With\n"
              << "                --config, use the given .hlsl-clippy.toml\n"
              << "                instead of walking up from the file's parent.\n"
              << "                With --target-profile, override the Slang\n"
              << "                reflection profile (default: per-stage sm_6_6).\n"
              << "                With --format, control output rendering:\n"
              << "                  human (default)        rustc-style with carets\n"
              << "                  json                   flat array, stable schema\n"
              << "                  github-annotations     ::warning file=...:: lines\n"
              << "                When $GITHUB_ACTIONS=true and --format is unset,\n"
              << "                github-annotations is selected automatically.\n"
              << "                With --source-language, force a frontend\n"
              << "                (default: auto, infers from extension).\n"
              << "                On `.slang` sources only Reflection-stage rules\n"
              << "                fire; tree-sitter-hlsl cannot parse Slang's\n"
              << "                language extensions (ADR 0020 sub-phase A).\n"
              << "  --help        Print this help\n"
              << "  --version     Print version\n";
}

[[nodiscard]] std::string_view severity_label(hlsl_clippy::Severity sev) noexcept {
    switch (sev) {
        case hlsl_clippy::Severity::Error:
            return "error";
        case hlsl_clippy::Severity::Warning:
            return "warning";
        case hlsl_clippy::Severity::Note:
            return "note";
    }
    return "warning";
}

void render_diagnostic(const hlsl_clippy::Diagnostic& diag,
                       const hlsl_clippy::SourceManager& sources,
                       std::ostream& out) {
    const hlsl_clippy::SourceFile* file = sources.get(diag.primary_span.source);
    const auto lo = diag.primary_span.bytes.lo;
    const auto hi = diag.primary_span.bytes.hi;
    const auto loc = sources.resolve(diag.primary_span.source, lo);

    const std::string path = file != nullptr ? file->path().string() : std::string{"<unknown>"};

    out << path << ':' << loc.line << ':' << loc.column << ": " << severity_label(diag.severity)
        << ": " << diag.message << " [" << diag.code << "]\n";

    if (file != nullptr) {
        const std::string_view line_text = file->line_text(lo);
        if (!line_text.empty()) {
            // Prefix with line number so the snippet is grep-friendly.
            out << "  " << loc.line << " | " << line_text << '\n';

            // Build the caret marker. Indent matches the prefix.
            const auto line_label_width = static_cast<std::size_t>(std::to_string(loc.line).size());
            const std::size_t caret_indent =
                2U + line_label_width + 3U + (loc.column > 0U ? loc.column - 1U : 0U);

            std::string caret_line;
            caret_line.append(caret_indent, ' ');
            const std::uint32_t span_len = hi > lo ? hi - lo : 1U;
            const std::uint32_t caret_max = static_cast<std::uint32_t>(line_text.size()) >
                                                    (loc.column > 0U ? loc.column - 1U : 0U)
                                                ? static_cast<std::uint32_t>(line_text.size()) -
                                                      (loc.column > 0U ? loc.column - 1U : 0U)
                                                : 1U;
            const std::uint32_t caret_count = std::min(span_len, caret_max);
            caret_line.append(static_cast<std::size_t>(caret_count == 0U ? 1U : caret_count), '^');
            out << caret_line << '\n';
        }
    }
}

// JSON string escaper. Hand-rolled to keep CLI link surface narrow (no
// nlohmann/json pull-in for the binary that does not need JSON-RPC framing).
// Handles the seven mandatory escapes plus `\uXXXX` for control bytes.
void json_escape(std::string_view s, std::ostream& out) {
    out << '"';
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c < 0x20U) {
                    constexpr std::array<char, 16> k_hex = {'0',
                                                            '1',
                                                            '2',
                                                            '3',
                                                            '4',
                                                            '5',
                                                            '6',
                                                            '7',
                                                            '8',
                                                            '9',
                                                            'a',
                                                            'b',
                                                            'c',
                                                            'd',
                                                            'e',
                                                            'f'};
                    out << "\\u00" << k_hex.at((c >> 4U) & 0x0FU) << k_hex.at(c & 0x0FU);
                } else {
                    out << raw;
                }
                break;
        }
    }
    out << '"';
}

void render_json_diagnostic(const hlsl_clippy::Diagnostic& diag,
                            const hlsl_clippy::SourceManager& sources,
                            std::ostream& out) {
    const hlsl_clippy::SourceFile* file = sources.get(diag.primary_span.source);
    const auto lo = diag.primary_span.bytes.lo;
    const auto hi = diag.primary_span.bytes.hi;
    const auto loc = sources.resolve(diag.primary_span.source, lo);
    const std::string path = file != nullptr ? file->path().string() : std::string{"<unknown>"};

    out << "{\"file\":";
    json_escape(path, out);
    out << ",\"line\":" << loc.line << ",\"column\":" << loc.column << ",\"byte_offset\":" << lo
        << ",\"byte_end\":" << hi << ",\"severity\":";
    json_escape(severity_label(diag.severity), out);
    out << ",\"rule\":";
    json_escape(diag.code, out);
    out << ",\"message\":";
    json_escape(diag.message, out);
    out << ",\"machine_applicable_fix\":";
    bool has_machine_fix = false;
    for (const auto& f : diag.fixes) {
        if (f.machine_applicable) {
            has_machine_fix = true;
            break;
        }
    }
    out << (has_machine_fix ? "true" : "false");
    out << '}';
}

// GitHub Actions workflow-command escape: `%`, `\r`, `\n` MUST be encoded
// in the message field so a multi-line rule message doesn't terminate the
// command early. See:
//   https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#example-setting-an-error-message
void github_escape(std::string_view s, std::ostream& out) {
    for (const char c : s) {
        switch (c) {
            case '%':
                out << "%25";
                break;
            case '\r':
                out << "%0D";
                break;
            case '\n':
                out << "%0A";
                break;
            default:
                out << c;
                break;
        }
    }
}

void render_github_annotation(const hlsl_clippy::Diagnostic& diag,
                              const hlsl_clippy::SourceManager& sources,
                              std::ostream& out) {
    const hlsl_clippy::SourceFile* file = sources.get(diag.primary_span.source);
    const auto lo = diag.primary_span.bytes.lo;
    const auto loc = sources.resolve(diag.primary_span.source, lo);
    const std::string path = file != nullptr ? file->path().string() : std::string{"<unknown>"};

    // Severity → workflow command. GH supports `error`, `warning`, `notice`.
    std::string_view command = "warning";
    if (diag.severity == hlsl_clippy::Severity::Error) {
        command = "error";
    } else if (diag.severity == hlsl_clippy::Severity::Note) {
        command = "notice";
    }

    out << "::" << command << " file=";
    github_escape(path, out);
    out << ",line=" << loc.line << ",col=" << loc.column << ",title=";
    github_escape(diag.code, out);
    out << "::";
    github_escape(diag.message, out);
    out << " [" << diag.code << "]\n";
}

[[nodiscard]] std::optional<OutputFormat> parse_format(std::string_view value) noexcept {
    if (value == "human") {
        return OutputFormat::Human;
    }
    if (value == "json") {
        return OutputFormat::Json;
    }
    if (value == "github-annotations") {
        return OutputFormat::GithubAnnotations;
    }
    return std::nullopt;
}

// Parse `--source-language=<auto|hlsl|slang>` (ADR 0020 sub-phase A).
// `auto` (default) defers the decision to per-file extension inference;
// explicit values force the corresponding frontend regardless of extension.
[[nodiscard]] std::optional<hlsl_clippy::SourceLanguage> parse_source_language(
    std::string_view value) noexcept {
    if (value == "auto") {
        return hlsl_clippy::SourceLanguage::Auto;
    }
    if (value == "hlsl") {
        return hlsl_clippy::SourceLanguage::Hlsl;
    }
    if (value == "slang") {
        return hlsl_clippy::SourceLanguage::Slang;
    }
    return std::nullopt;
}

// Auto-detect: if the user did not pass `--format`, default to
// `github-annotations` when running inside a GitHub Actions runner so
// `hlsl-clippy lint shader.hlsl` Just Works as a CI step. Outside Actions,
// keep `human` for interactive shell use.
[[nodiscard]] OutputFormat detect_default_format() noexcept {
    const char* gh = std::getenv("GITHUB_ACTIONS");  // NOLINT(concurrency-mt-unsafe)
    if (gh != nullptr && std::string_view{gh} == "true") {
        return OutputFormat::GithubAnnotations;
    }
    return OutputFormat::Human;
}

struct LintOptions {
    std::vector<std::string> paths;  ///< One or more shader files; lint runs in argv order.
    bool apply_fix = false;
    std::string config_path;             ///< Empty means walk-up resolution.
    std::string target_profile;          ///< Empty means per-stage default profile.
    std::optional<OutputFormat> format;  ///< std::nullopt → resolve via env at runtime.
    /// `--source-language=<auto|hlsl|slang>` (ADR 0020 sub-phase A — v1.3).
    /// `Auto` defers to per-file extension inference; explicit `Hlsl` or
    /// `Slang` overrides the inference for every path passed in argv.
    hlsl_clippy::SourceLanguage source_language = hlsl_clippy::SourceLanguage::Auto;
};

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

bool write_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return stream.good();
}

[[nodiscard]] std::vector<hlsl_clippy::PrioritisedFix> collect_fixes(
    const std::vector<hlsl_clippy::Diagnostic>& diagnostics) {
    std::vector<hlsl_clippy::PrioritisedFix> out;
    for (const auto& d : diagnostics) {
        for (const auto& f : d.fixes) {
            if (!f.machine_applicable) {
                continue;
            }
            hlsl_clippy::PrioritisedFix pf;
            pf.priority = hlsl_clippy::FixPriority{.severity = d.severity, .rule_id = d.code};
            pf.fix = f;
            out.push_back(std::move(pf));
        }
    }
    return out;
}

struct PerFileResult {
    bool any_warning = false;
    bool any_error = false;
    bool fatal = false;  ///< file-not-found / read-failure / config-error.
    std::size_t diag_count = 0;
};

/// Lint one file. State that's safe to share across files (rules pack, the
/// global Slang IGlobalSession behind ReflectionEngine, the CFG engine
/// cache) is reused via process-lifetime singletons; only the
/// SourceManager + Config are per-file. JSON-format callers pass
/// `json_state` so the streaming `[..., {}, {}]` array is built across
/// all files in one go (one outer array, comma-separated).
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] PerFileResult run_lint_one(const LintOptions& opts,
                                         const std::filesystem::path& path,
                                         OutputFormat format,
                                         bool& json_first_seen) {
    PerFileResult result;

    if (!std::filesystem::exists(path)) {
        std::cerr << "hlsl-clippy: file not found: " << path.string() << '\n';
        result.fatal = true;
        return result;
    }

    hlsl_clippy::SourceManager sources;
    const auto src_id = sources.add_file(path);
    if (!src_id.valid()) {
        std::cerr << "hlsl-clippy: could not read " << path.string() << '\n';
        result.fatal = true;
        return result;
    }

    auto rules = hlsl_clippy::make_default_rules();

    // Resolve a config: explicit `--config` first, then per-file walk-up.
    // Walk-up is per-file deliberately — a multi-file invocation can span
    // multiple workspaces with their own `.hlsl-clippy.toml`.
    std::optional<hlsl_clippy::Config> config;
    std::filesystem::path resolved_config_path;
    if (!opts.config_path.empty()) {
        resolved_config_path = opts.config_path;
    } else if (auto found = hlsl_clippy::find_config(path); found.has_value()) {
        resolved_config_path = *found;
    }
    if (!resolved_config_path.empty()) {
        auto config_result = hlsl_clippy::load_config(resolved_config_path);
        if (!config_result) {
            const auto& err = config_result.error();
            std::cerr << err.source.string() << ':' << err.line << ':' << err.column
                      << ": error: " << err.message << " [clippy::config]\n";
            result.fatal = true;
            return result;
        }
        config = std::move(config_result).value();
    }

    hlsl_clippy::LintOptions lint_options;
    if (!opts.target_profile.empty()) {
        lint_options.target_profile = opts.target_profile;
    }

    // ADR 0020 sub-phase A: if the CLI passed an explicit
    // `--source-language=<hlsl|slang>` override, plumb it through the Config
    // surface so the orchestrator's per-file inference is bypassed for
    // every path in this invocation. We only mutate the Config when the
    // override is non-Auto: leaving it Auto preserves whatever the TOML
    // declared (or, if no TOML exists, the orchestrator's default
    // extension-based inference).
    std::optional<hlsl_clippy::Config> effective_config = config;
    if (opts.source_language != hlsl_clippy::SourceLanguage::Auto) {
        if (!effective_config.has_value()) {
            effective_config.emplace();
        }
        effective_config->source_language_value = opts.source_language;
    }

    const auto diagnostics =
        effective_config.has_value()
            ? hlsl_clippy::lint(sources, src_id, rules, *effective_config, path, lint_options)
            : hlsl_clippy::lint(sources, src_id, rules, lint_options);

    result.diag_count = diagnostics.size();

    if (format == OutputFormat::Json) {
        for (const auto& diag : diagnostics) {
            if (json_first_seen) {
                std::cout << ',';
            }
            json_first_seen = true;
            render_json_diagnostic(diag, sources, std::cout);
        }
    } else {
        for (const auto& diag : diagnostics) {
            if (format == OutputFormat::GithubAnnotations) {
                render_github_annotation(diag, sources, std::cout);
            } else {
                render_diagnostic(diag, sources, std::cout);
            }
        }
    }

    for (const auto& diag : diagnostics) {
        if (diag.severity == hlsl_clippy::Severity::Error) {
            result.any_error = true;
        } else if (diag.severity == hlsl_clippy::Severity::Warning) {
            result.any_warning = true;
        }
    }

    // Apply fixes per-file (the rewrite is local to one source).
    if (opts.apply_fix) {
        const auto fixes = collect_fixes(diagnostics);
        if (fixes.empty()) {
            // Per-file no-op note only emitted in human format to keep
            // JSON/GH output strictly machine-parseable.
            if (format == OutputFormat::Human) {
                std::cout << "hlsl-clippy: --fix had nothing to apply for " << path.string()
                          << '\n';
            }
        } else {
            const std::string original = read_file(path);
            if (original.empty() && !std::filesystem::is_empty(path)) {
                std::cerr << "hlsl-clippy: could not read " << path.string() << " for --fix\n";
                result.fatal = true;
                return result;
            }
            const hlsl_clippy::Rewriter rewriter;
            std::vector<hlsl_clippy::FixConflict> conflicts;
            const std::string rewritten = rewriter.apply(original, fixes, &conflicts);
            if (!write_file(path, rewritten)) {
                std::cerr << "hlsl-clippy: could not write " << path.string() << '\n';
                result.fatal = true;
                return result;
            }
            if (format == OutputFormat::Human) {
                const auto applied_count = fixes.size() - conflicts.size();
                std::cout << "hlsl-clippy: applied " << applied_count << " fix"
                          << (applied_count == 1U ? "" : "es") << " to " << path.string() << '\n';
            }
            for (const auto& c : conflicts) {
                std::cerr << path.string() << ": note: dropped fix from `" << c.dropped_rule_id
                          << "` because it overlaps `" << c.winning_rule_id
                          << "` [clippy::fix-conflict]\n";
            }
        }
    }

    return result;
}

[[nodiscard]] int run_lint(const LintOptions& opts) {
    if (opts.paths.empty()) {
        std::cerr << "hlsl-clippy: lint requires at least one file argument\n";
        return 2;
    }

    const OutputFormat format = opts.format.value_or(detect_default_format());

    bool any_warning = false;
    bool any_error = false;
    bool any_fatal = false;
    std::size_t total_diags = 0;
    bool json_first_seen = false;

    if (format == OutputFormat::Json) {
        std::cout << '[';
    }

    for (const auto& path_str : opts.paths) {
        const std::filesystem::path path{path_str};
        const PerFileResult r = run_lint_one(opts, path, format, json_first_seen);
        if (r.fatal) {
            any_fatal = true;
        }
        if (r.any_error) {
            any_error = true;
        }
        if (r.any_warning) {
            any_warning = true;
        }
        total_diags += r.diag_count;
    }

    if (format == OutputFormat::Json) {
        std::cout << "]\n";
    } else if (format == OutputFormat::Human && total_diags > 0U) {
        std::cout << '\n'
                  << "hlsl-clippy: " << total_diags << " diagnostic"
                  << (total_diags == 1U ? "" : "s") << " emitted across " << opts.paths.size()
                  << (opts.paths.size() == 1U ? " file\n" : " files\n");
    }

    if (any_error || any_fatal) {
        return 2;
    }
    if (any_warning) {
        return 1;
    }
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] int parse_lint_args(std::span<const std::string_view> args, LintOptions& opts) {
    // args here is the tail after the `lint` subcommand.
    if (args.empty()) {
        std::cerr << "hlsl-clippy: lint requires a file argument\n";
        return 2;
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto a = args[i];
        if (a == "--fix") {
            opts.apply_fix = true;
            continue;
        }
        if (a == "--config") {
            if (i + 1U >= args.size()) {
                std::cerr << "hlsl-clippy: --config requires a path argument\n";
                return 2;
            }
            opts.config_path = std::string{args[i + 1U]};
            ++i;
            continue;
        }
        if (a == "--target-profile") {
            if (i + 1U >= args.size()) {
                std::cerr << "hlsl-clippy: --target-profile requires a profile argument\n";
                return 2;
            }
            opts.target_profile = std::string{args[i + 1U]};
            ++i;
            continue;
        }
        // `--source-language=<auto|hlsl|slang>` (ADR 0020 sub-phase A).
        // Single-token + two-token forms both supported.
        if (a.starts_with("--source-language=")) {
            const auto value = a.substr(std::string_view{"--source-language="}.size());
            const auto parsed = parse_source_language(value);
            if (!parsed) {
                std::cerr << "hlsl-clippy: unknown --source-language value: " << value
                          << " (expected auto|hlsl|slang)\n";
                return 2;
            }
            opts.source_language = *parsed;
            continue;
        }
        if (a == "--source-language") {
            if (i + 1U >= args.size()) {
                std::cerr << "hlsl-clippy: --source-language requires a value (auto|hlsl|slang)\n";
                return 2;
            }
            const auto parsed = parse_source_language(args[i + 1U]);
            if (!parsed) {
                std::cerr << "hlsl-clippy: unknown --source-language value: " << args[i + 1U]
                          << " (expected auto|hlsl|slang)\n";
                return 2;
            }
            opts.source_language = *parsed;
            ++i;
            continue;
        }
        // `--format=value` (single-token form, conventional for CI).
        if (a.starts_with("--format=")) {
            const auto value = a.substr(std::string_view{"--format="}.size());
            const auto parsed = parse_format(value);
            if (!parsed) {
                std::cerr << "hlsl-clippy: unknown --format value: " << value
                          << " (expected human|json|github-annotations)\n";
                return 2;
            }
            opts.format = parsed;
            continue;
        }
        // `--format value` (two-token form, also supported).
        if (a == "--format") {
            if (i + 1U >= args.size()) {
                std::cerr
                    << "hlsl-clippy: --format requires a value (human|json|github-annotations)\n";
                return 2;
            }
            const auto parsed = parse_format(args[i + 1U]);
            if (!parsed) {
                std::cerr << "hlsl-clippy: unknown --format value: " << args[i + 1U]
                          << " (expected human|json|github-annotations)\n";
                return 2;
            }
            opts.format = parsed;
            ++i;
            continue;
        }
        if (a.starts_with("--")) {
            std::cerr << "hlsl-clippy: unknown lint option: " << a << '\n';
            return 2;
        }
        // Positional argument — accumulate as a shader path. Multi-file
        // invocations amortize Slang init / rule registry / reflection
        // cache across all files in one process. Useful for tree-wide CI
        // gates (`hlsl-clippy lint shaders/**/*.hlsl`).
        opts.paths.emplace_back(a);
    }
    if (opts.paths.empty()) {
        std::cerr << "hlsl-clippy: lint requires at least one file argument\n";
        return 2;
    }
    return -1;  // sentinel: continue with run_lint.
}

[[nodiscard]] int run_main(int argc, char** argv) {
    // Collect arguments into a vector so we don't index into argv directly in
    // multiple places (clang-tidy doesn't love raw pointer arithmetic).
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }

    if (args.size() < 2U) {
        print_usage();
        return 1;
    }
    const std::string_view cmd = args[1];
    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }
    if (cmd == "--version" || cmd == "-v") {
        std::cout << hlsl_clippy::version() << "\n";
        return 0;
    }
    if (cmd == "lint") {
        LintOptions opts;
        const auto tail = std::span<const std::string_view>(args).subspan(2U, args.size() - 2U);
        if (const int rc = parse_lint_args(tail, opts); rc >= 0) {
            return rc;
        }
        return run_lint(opts);
    }
    print_usage();
    return 1;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape) -- catch-all guarantees no escape.
int main(int argc, char** argv) {
    try {
        return run_main(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "hlsl-clippy: fatal: " << ex.what() << '\n';
        return 2;
    } catch (...) {
        std::cerr << "hlsl-clippy: fatal: unknown exception\n";
        return 2;
    }
}
