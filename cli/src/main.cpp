#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/version.hpp"

namespace {

void print_usage() {
    std::cout << "hlsl-clippy " << hlsl_clippy::version() << "\n"
              << "Usage: hlsl-clippy <command> [args]\n"
              << "\n"
              << "Commands:\n"
              << "  lint <file>   Lint an HLSL source file\n"
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

[[nodiscard]] int run_lint(const std::string& path_arg) {
    const std::filesystem::path path{path_arg};
    if (!std::filesystem::exists(path)) {
        std::cerr << "hlsl-clippy: file not found: " << path_arg << '\n';
        return 2;
    }

    hlsl_clippy::SourceManager sources;
    const auto src_id = sources.add_file(path);
    if (!src_id.valid()) {
        std::cerr << "hlsl-clippy: could not read " << path_arg << '\n';
        return 2;
    }

    auto rules = hlsl_clippy::make_default_rules();
    const auto diagnostics = hlsl_clippy::lint(sources, src_id, rules);

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

    if (any_error) {
        return 2;
    }
    if (any_warning) {
        return 1;
    }
    return 0;
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
        if (args.size() < 3U) {
            std::cerr << "hlsl-clippy: lint requires a file argument\n";
            return 2;
        }
        return run_lint(std::string{args[2]});
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
