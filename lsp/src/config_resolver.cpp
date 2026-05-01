// Config resolver implementation.

#include "config_resolver.hpp"

#include <filesystem>
#include <optional>
#include <utility>

#include "hlsl_clippy/config.hpp"

namespace hlsl_clippy::lsp {

std::optional<hlsl_clippy::Config> resolve_config_for(const std::filesystem::path& document_path) {
    const auto config_path = hlsl_clippy::find_config(document_path);
    if (!config_path.has_value()) {
        return std::nullopt;
    }
    auto result = hlsl_clippy::load_config(*config_path);
    if (!result) {
        // Parse error — silently fall back. A future sub-phase can surface
        // this as a workspace-level diagnostic ("clippy::config" code) once
        // the LSP gains a `window/showMessage` channel for non-document
        // errors. For 5a, swallowing keeps the editor responsive.
        return std::nullopt;
    }
    return std::move(result).value();
}

}  // namespace hlsl_clippy::lsp
