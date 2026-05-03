// shader_clippy::Diagnostic → LSP Diagnostic JSON conversion.
//
// LSP Diagnostic shape (v3.17 §"Diagnostic"):
//   {
//     "range": { "start": {"line": int, "character": int},
//                "end":   {"line": int, "character": int} },
//     "severity": int,            // 1=Error, 2=Warning, 3=Information, 4=Hint
//     "code": string,             // rule id
//     "source": "shader-clippy",
//     "message": string
//   }

#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::lsp::server {

/// Map a single shader_clippy::Diagnostic to an LSP Diagnostic JSON object.
[[nodiscard]] nlohmann::json to_lsp_diagnostic(const shader_clippy::Diagnostic& diag,
                                               const shader_clippy::SourceManager& sources);

/// Map a collection of diagnostics. Order is preserved.
[[nodiscard]] nlohmann::json to_lsp_diagnostics(
    const std::vector<shader_clippy::Diagnostic>& diagnostics,
    const shader_clippy::SourceManager& sources);

}  // namespace shader_clippy::lsp::server
