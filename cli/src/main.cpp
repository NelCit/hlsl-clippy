#include <algorithm>
#include <cstddef>
#include <cstdint>
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
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rewriter.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/version.hpp"

namespace {

void print_usage() {
    std::cout << "hlsl-clippy " << hlsl_clippy::version() << "\n"
              << "Usage: hlsl-clippy <command> [args]\n"
              << "\n"
              << "Commands:\n"
              << "  lint <file> [--fix] [--config <path>]\n"
              << "                Lint an HLSL source file. With --fix, apply\n"
              << "                machine-applicable rewrites in place. With\n"
              << "                --config, use the given .hlsl-clippy.toml\n"
              << "                instead of walking up from the file's parent.\n"
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

struct LintOptions {
    std::string path;
    bool apply_fix = false;
    std::string config_path;  ///< Empty means walk-up resolution.
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

[[nodiscard]] int run_lint(const LintOptions& opts) {
    const std::filesystem::path path{opts.path};
    if (!std::filesystem::exists(path)) {
        std::cerr << "hlsl-clippy: file not found: " << opts.path << '\n';
        return 2;
    }

    hlsl_clippy::SourceManager sources;
    const auto src_id = sources.add_file(path);
    if (!src_id.valid()) {
        std::cerr << "hlsl-clippy: could not read " << opts.path << '\n';
        return 2;
    }

    auto rules = hlsl_clippy::make_default_rules();

    // Resolve a config: explicit `--config` first, then walk-up.
    std::optional<hlsl_clippy::Config> config;
    std::filesystem::path resolved_config_path;
    if (!opts.config_path.empty()) {
        resolved_config_path = opts.config_path;
    } else if (auto found = hlsl_clippy::find_config(path); found.has_value()) {
        resolved_config_path = *found;
    }
    if (!resolved_config_path.empty()) {
        auto result = hlsl_clippy::load_config(resolved_config_path);
        if (!result) {
            const auto& err = result.error();
            std::cerr << err.source.string() << ':' << err.line << ':' << err.column
                      << ": error: " << err.message << " [clippy::config]\n";
            return 2;
        }
        config = std::move(result).value();
    }

    const auto diagnostics = config.has_value()
                                 ? hlsl_clippy::lint(sources, src_id, rules, *config, path)
                                 : hlsl_clippy::lint(sources, src_id, rules);

    bool any_warning = false;
    bool any_error = false;
    for (const auto& diag : diagnostics) {
        render_diagnostic(diag, sources, std::cout);
        if (diag.severity == hlsl_clippy::Severity::Error) {
            any_error = true;
        } else if (diag.severity == hlsl_clippy::Severity::Warning) {
            any_warning = true;
        }
    }

    if (!diagnostics.empty()) {
        std::cout << '\n'
                  << "hlsl-clippy: " << diagnostics.size() << " diagnostic"
                  << (diagnostics.size() == 1U ? "" : "s") << " emitted\n";
    }

    // Apply fixes after rendering diagnostics so users see what the rewrite
    // addressed. We re-read the file from disk to get the original bytes
    // (the SourceManager copy is fine but reading again keeps the rewrite
    // path fully decoupled from the lint pipeline).
    if (opts.apply_fix) {
        const auto fixes = collect_fixes(diagnostics);
        if (fixes.empty()) {
            std::cout << "hlsl-clippy: --fix had nothing to apply\n";
        } else {
            const std::string original = read_file(path);
            if (original.empty() && !std::filesystem::is_empty(path)) {
                std::cerr << "hlsl-clippy: could not read " << opts.path << " for --fix\n";
                return 2;
            }
            const hlsl_clippy::Rewriter rewriter;
            std::vector<hlsl_clippy::FixConflict> conflicts;
            const std::string rewritten = rewriter.apply(original, fixes, &conflicts);
            if (!write_file(path, rewritten)) {
                std::cerr << "hlsl-clippy: could not write " << opts.path << '\n';
                return 2;
            }
            std::cout << "hlsl-clippy: applied " << (fixes.size() - conflicts.size()) << " fix"
                      << (fixes.size() - conflicts.size() == 1U ? "" : "es") << " to " << opts.path
                      << '\n';
            for (const auto& c : conflicts) {
                std::cerr << opts.path << ": note: dropped fix from `" << c.dropped_rule_id
                          << "` because it overlaps `" << c.winning_rule_id
                          << "` [clippy::fix-conflict]\n";
            }
        }
    }

    if (any_error) {
        return 2;
    }
    if (any_warning) {
        return 1;
    }
    return 0;
}

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
        if (a.starts_with("--")) {
            std::cerr << "hlsl-clippy: unknown lint option: " << a << '\n';
            return 2;
        }
        if (opts.path.empty()) {
            opts.path = std::string{a};
            continue;
        }
        std::cerr << "hlsl-clippy: unexpected positional argument: " << a << '\n';
        return 2;
    }
    if (opts.path.empty()) {
        std::cerr << "hlsl-clippy: lint requires a file argument\n";
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
