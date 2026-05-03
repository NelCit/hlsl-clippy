// shader_clippy::Fix → LSP CodeAction / WorkspaceEdit conversion.
//
// Sub-phase 5b (per ADR 0014 §"Sub-phase 5b — Code actions"). Each
// `Diagnostic` carrying one or more `Fix`es is mapped to an LSP CodeAction:
//
//   {
//     "title":       "Apply quick-fix: <Fix::description>",
//     "kind":        "quickfix",
//     "diagnostics": [ <LSP Diagnostic JSON> ],
//     "edit": {
//       "changes": {
//         "<document_uri>": [
//           { "range": { "start": {...}, "end": {...} },
//             "newText": "<Fix::edits[i].replacement>" },
//           ...
//         ]
//       }
//     },
//     "isPreferred": <Fix::machine_applicable>
//   }
//
// The range filter mirrors VS Code's behaviour: a code action is offered
// only when the diagnostic's primary span overlaps the editor selection /
// cursor range that the client passed in `params.range`.
//
// Hover support — `hover_for_position` — returns a markdown hover whose body
// contains the rule id, a docs link, and the diagnostic message. The link
// pattern is hard-coded here per ADR 0014; the docs site (Phase 6) is not
// yet live, so we point at the GitHub-hosted markdown source instead.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "shader_clippy/diagnostic.hpp"
#include "shader_clippy/source.hpp"

namespace shader_clippy::lsp::server {

/// Convert each Diagnostic with a Fix into an LSP CodeAction. Diagnostics
/// whose `primary_span` does not overlap the requested range
/// `[(line_start, char_start), (line_end, char_end))` are excluded.
///
/// The returned value is always a JSON array; an empty array is the LSP
/// "no actions available" response.
[[nodiscard]] nlohmann::json code_actions_for_range(
    const std::vector<shader_clippy::Diagnostic>& diagnostics,
    const shader_clippy::SourceManager& sources,
    std::string_view document_uri,
    std::int32_t requested_line_start,
    std::int32_t requested_char_start,
    std::int32_t requested_line_end,
    std::int32_t requested_char_end);

/// Build a hover response for the diagnostic whose primary span contains
/// the requested position. Returns a JSON null when no diagnostic covers
/// the position (LSP-spec way of saying "no hover content available").
///
/// Output shape per LSP §"Hover":
///   { "contents": { "kind": "markdown", "value": "..." } }
[[nodiscard]] nlohmann::json hover_for_position(
    const std::vector<shader_clippy::Diagnostic>& diagnostics,
    const shader_clippy::SourceManager& sources,
    std::int32_t line,
    std::int32_t character);

/// Hard-coded URL pattern for the per-rule documentation page. Pre-Phase-6
/// (no docs site live yet) we link to the GitHub-hosted markdown source.
[[nodiscard]] std::string docs_url_for_rule(std::string_view rule_id);

}  // namespace shader_clippy::lsp::server
