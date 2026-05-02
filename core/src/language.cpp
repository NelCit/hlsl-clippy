// Source-language detection per ADR 0020 sub-phase A (v1.3.0).
//
// The detection rule is intentionally simple: look at the path's extension
// (case-insensitive on ASCII), match `.slang` -> Slang, anything else -> Hlsl.
// Slang's reflection bridge does the rest: passing a `.slang`-suffixed
// virtual_path to `loadModuleFromSourceString` engages Slang's native frontend.
//
// This helper is reachable from both the CLI (`cli/src/main.cpp`) and the LSP
// (`lsp/src/server/handlers.cpp`) so a single source of truth governs how a
// given path's language is resolved.

#include "hlsl_clippy/language.hpp"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace hlsl_clippy {

namespace {

/// ASCII-lowercase a string copy; non-ASCII bytes pass through. The detection
/// path only ever inspects shader-extension strings, which are pure ASCII in
/// every codebase we know about, so a Unicode-aware fold would be overkill.
[[nodiscard]] std::string ascii_to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(static_cast<char>(std::tolower(u)));
    }
    return out;
}

}  // namespace

SourceLanguage detect_language(const std::filesystem::path& path) noexcept {
    // `path::extension()` returns the trailing component including the dot
    // (e.g. ".slang"). For a path with no extension this returns an empty
    // string, which falls through to the Hlsl default.
    const std::string ext_str = path.extension().string();
    const std::string lower = ascii_to_lower(ext_str);
    if (lower == ".slang") {
        return SourceLanguage::Slang;
    }
    return SourceLanguage::Hlsl;
}

SourceLanguage resolve_language(SourceLanguage selected,
                                const std::filesystem::path& path) noexcept {
    if (selected == SourceLanguage::Auto) {
        return detect_language(path);
    }
    return selected;
}

std::string_view language_label(SourceLanguage lang) noexcept {
    switch (lang) {
        case SourceLanguage::Auto:
            return "auto";
        case SourceLanguage::Hlsl:
            return "hlsl";
        case SourceLanguage::Slang:
            return "slang";
    }
    return "hlsl";
}

}  // namespace hlsl_clippy
