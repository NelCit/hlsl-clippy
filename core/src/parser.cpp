#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "hlsl_clippy/language.hpp"
#include "hlsl_clippy/source.hpp"

#include "parser_internal.hpp"

// ADR 0021 sub-phase B.2 (v1.4.0) — parser dispatch by source language. The
// HLSL grammar handles `.hlsl`, `.hlsli`, `.fx*`, and any unknown extension
// (conservative pre-v1.3 default). The Slang grammar handles `.slang`. The
// two C symbols coexist: each grammar's `tree_sitter_<lang>()` factory
// returns a distinct `TSLanguage*` linked against the shared tree-sitter
// runtime.
extern "C" {
const ::TSLanguage* tree_sitter_hlsl(void);
const ::TSLanguage* tree_sitter_slang(void);
}  // extern "C"

namespace hlsl_clippy::parser {

namespace {

/// Pick the tree-sitter grammar for a resolved source language. Slang sources
/// route through `tree_sitter_slang()` (ADR 0021 sub-phase B); everything else
/// keeps the historical tree-sitter-hlsl path. `Auto` should never reach this
/// helper — the orchestrator resolves it upstream — but we treat it as HLSL
/// to be safe.
[[nodiscard]] const ::TSLanguage* select_language(SourceLanguage lang) noexcept {
    switch (lang) {
        case SourceLanguage::Slang:
            return tree_sitter_slang();
        case SourceLanguage::Hlsl:
        case SourceLanguage::Auto:
            break;
    }
    return tree_sitter_hlsl();
}

}  // namespace

std::optional<ParsedSource> parse(const SourceManager& sources, SourceId source) {
    const SourceFile* file = sources.get(source);
    if (file == nullptr) {
        return std::nullopt;
    }

    // ADR 0021 sub-phase B.2 — resolve the per-file source language by
    // extension. The orchestrator separately tracks the resolved language
    // (it consults `Config::source_language()` and falls back to inference);
    // the parser path here is reachable from both `lint()` calls and the
    // shared CFG engine's reparse helpers, so we re-derive the language
    // locally rather than threading it through every call site. The
    // detection function is pure-functional and trivially fast — a single
    // extension comparison.
    const SourceLanguage lang = detect_language(file->path());
    const ::TSLanguage* grammar = select_language(lang);

    const UniqueParser parser{ts_parser_new()};
    if (!parser) {
        return std::nullopt;
    }

    if (!ts_parser_set_language(parser.get(), grammar)) {
        return std::nullopt;
    }

    const std::string_view bytes = file->contents();
    UniqueTree tree{ts_parser_parse_string(
        parser.get(), nullptr, bytes.data(), static_cast<std::uint32_t>(bytes.size()))};

    if (!tree) {
        return std::nullopt;
    }

    return ParsedSource{
        .source = source, .bytes = bytes, .tree = std::move(tree), .language = grammar};
}

}  // namespace hlsl_clippy::parser
