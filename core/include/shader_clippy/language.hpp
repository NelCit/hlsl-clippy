#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace shader_clippy {

/// Source language selector (ADR 0020 sub-phase A — v1.3.0).
///
/// `Auto` defers the decision to per-file extension inference via
/// `detect_language()`. `Hlsl` and `Slang` force a specific frontend
/// regardless of the file's extension; useful as a CLI override
/// (`--source-language=...`) and as a `[lint] source-language` knob in
/// `.shader-clippy.toml`.
///
/// The orchestrator gates AST + CFG + IR rule dispatch on the resolved
/// language: when `Slang`, only `Stage::Reflection` rules run because
/// tree-sitter-hlsl cannot parse Slang's language extensions
/// (`__generic`, `interface`, `extension`, `associatedtype`, `import`).
/// Slang's reflection bridge is language-agnostic — it picks its native
/// frontend based on the virtual-path extension passed to
/// `loadModuleFromSourceString`.
enum class SourceLanguage : std::uint8_t {
    Auto,
    Hlsl,
    Slang,
};

/// Infer the source language from `path`'s extension. The extension match is
/// case-insensitive on the ASCII portion (so `.SLANG`, `.Slang`, `.slang`
/// all resolve to `SourceLanguage::Slang`). Unknown extensions resolve to
/// `SourceLanguage::Hlsl` — the conservative pre-v1.3 default.
///
/// Use this helper when `SourceLanguage::Auto` has been selected by the
/// caller (CLI default, LSP default, missing TOML key) to determine which
/// frontend the orchestrator should engage for a given source.
[[nodiscard]] SourceLanguage detect_language(const std::filesystem::path& path) noexcept;

/// Resolve `selected` against `path`. When `selected == Auto`, dispatch to
/// `detect_language(path)`; otherwise return `selected` unchanged. The
/// orchestrator calls this exactly once per lint run per source so both the
/// CLI and the LSP server land at the same resolved language.
[[nodiscard]] SourceLanguage resolve_language(SourceLanguage selected,
                                              const std::filesystem::path& path) noexcept;

/// Human-readable label used in diagnostic messages and CLI output.
[[nodiscard]] std::string_view language_label(SourceLanguage lang) noexcept;

}  // namespace shader_clippy
