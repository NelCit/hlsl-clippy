// Wraps core's `find_config` / `load_config` pair in a small helper
// suitable for the LSP context. Per ADR 0014 §"Workspace + config" the LSP
// re-uses the existing walk-up resolver verbatim.

#pragma once

#include <filesystem>
#include <optional>

#include "shader_clippy/config.hpp"

namespace shader_clippy::lsp {

/// Resolve a `Config` for `document_path` by walking up to the nearest
/// `.shader-clippy.toml`. Returns `std::nullopt` when no config is found.
/// On parse failure, returns `std::nullopt` as well — the caller falls back
/// to the default rule set rather than refusing to lint.
[[nodiscard]] std::optional<shader_clippy::Config> resolve_config_for(
    const std::filesystem::path& document_path);

}  // namespace shader_clippy::lsp
