// hlsl_clippy::Diagnostic → LSP Diagnostic JSON conversion.
//
// LSP Diagnostic shape (v3.17 §"Diagnostic"):
//   {
//     "range": { "start": {"line": int, "character": int},
//                "end":   {"line": int, "character": int} },
//     "severity": int,            // 1=Error, 2=Warning, 3=Information, 4=Hint
//     "code": string,             // rule id
//     "source": "hlsl-clippy",
//     "message": string
//   }

#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/source.hpp"

namespace hlsl_clippy::lsp::server {

/// Map a single hlsl_clippy::Diagnostic to an LSP Diagnostic JSON object.
[[nodiscard]] nlohmann::json to_lsp_diagnostic(const hlsl_clippy::Diagnostic& diag,
                                               const hlsl_clippy::SourceManager& sources);

/// Map a collection of diagnostics. Order is preserved.
[[nodiscard]] nlohmann::json to_lsp_diagnostics(
    const std::vector<hlsl_clippy::Diagnostic>& diagnostics,
    const hlsl_clippy::SourceManager& sources);

}  // namespace hlsl_clippy::lsp::server
