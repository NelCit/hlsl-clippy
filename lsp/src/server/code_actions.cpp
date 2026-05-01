// Code-action and hover conversion implementation (sub-phase 5b per
// ADR 0014).

#include "server/code_actions.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/source.hpp"
#include "server/diagnostic_convert.hpp"

namespace hlsl_clippy::lsp::server {

namespace {

/// Pre-Phase-6 docs URL prefix. ADR 0014 §"Sub-phase 5b" specifies that the
/// permanent URL pattern is `https://nelcit.github.io/hlsl-clippy/rules/...`
/// once the docs site ships in Phase 6, but for now we link to the raw
/// markdown source on GitHub so the link is never broken.
constexpr std::string_view k_docs_url_prefix =
    "https://github.com/NelCit/hlsl-clippy/blob/main/docs/rules/";

/// LSP `CodeActionKind` literal for quick-fixes (per LSP §"CodeActionKind").
constexpr std::string_view k_quickfix_kind = "quickfix";

/// One LSP position — a 0-based (line, character) pair.
struct Position {
    std::int32_t line = 0;
    std::int32_t character = 0;

    [[nodiscard]] friend constexpr bool operator<=(Position lhs, Position rhs) noexcept {
        return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character <= rhs.character);
    }
    [[nodiscard]] friend constexpr bool operator<(Position lhs, Position rhs) noexcept {
        return lhs.line < rhs.line || (lhs.line == rhs.line && lhs.character < rhs.character);
    }
};

/// Translate a (SourceId, byte-offset) into an LSP-style 0-based Position.
[[nodiscard]] Position to_position(const hlsl_clippy::SourceManager& sources,
                                   hlsl_clippy::SourceId source_id,
                                   std::uint32_t byte_offset) {
    const auto loc = sources.resolve(source_id, byte_offset);
    Position p{};
    p.line = loc.line > 0U ? static_cast<std::int32_t>(loc.line - 1U) : 0;
    p.character = loc.column > 0U ? static_cast<std::int32_t>(loc.column - 1U) : 0;
    return p;
}

/// Build the JSON object for one LSP Position.
[[nodiscard]] nlohmann::json position_to_json(Position p) {
    nlohmann::json obj = nlohmann::json::object();
    obj["line"] = p.line;
    obj["character"] = p.character;
    return obj;
}

/// Build a `{start, end}` LSP Range JSON object.
[[nodiscard]] nlohmann::json make_range(Position start, Position end) {
    nlohmann::json range = nlohmann::json::object();
    range["start"] = position_to_json(start);
    range["end"] = position_to_json(end);
    return range;
}

/// True when `[a_lo, a_hi)` and `[b_lo, b_hi)` share at least one position.
/// A zero-width range at a position counts as overlapping that position
/// (this matches how VS Code treats a bare cursor inside a diagnostic
/// span).
[[nodiscard]] bool ranges_overlap(Position a_lo,
                                  Position a_hi,
                                  Position b_lo,
                                  Position b_hi) noexcept {
    // Standard half-open overlap, with the cursor-inside-span edge case:
    // if either range is degenerate (lo == hi), treat it as covering its
    // single point inclusively.
    const bool a_degenerate = !(a_lo < a_hi);
    const bool b_degenerate = !(b_lo < b_hi);
    if (a_degenerate && b_degenerate) {
        return a_lo.line == b_lo.line && a_lo.character == b_lo.character;
    }
    if (a_degenerate) {
        return b_lo <= a_lo && a_lo <= b_hi;
    }
    if (b_degenerate) {
        return a_lo <= b_lo && b_lo <= a_hi;
    }
    // Both non-degenerate: half-open overlap.
    return a_lo < b_hi && b_lo < a_hi;
}

/// Map one Fix's edits into the WorkspaceEdit `changes[uri]` array.
[[nodiscard]] nlohmann::json fix_edits_to_text_edits(const hlsl_clippy::Fix& fix,
                                                     const hlsl_clippy::SourceManager& sources) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& edit : fix.edits) {
        const auto start = to_position(sources, edit.span.source, edit.span.bytes.lo);
        const auto end = to_position(sources, edit.span.source, edit.span.bytes.hi);
        nlohmann::json te = nlohmann::json::object();
        te["range"] = make_range(start, end);
        te["newText"] = edit.replacement;
        arr.push_back(std::move(te));
    }
    return arr;
}

/// Build one CodeAction JSON object for a (diagnostic, fix) pair.
[[nodiscard]] nlohmann::json build_code_action(const hlsl_clippy::Diagnostic& diag,
                                               const hlsl_clippy::Fix& fix,
                                               const hlsl_clippy::SourceManager& sources,
                                               std::string_view document_uri) {
    nlohmann::json action = nlohmann::json::object();
    action["title"] = std::string{"Apply quick-fix: "} + fix.description;
    action["kind"] = std::string{k_quickfix_kind};

    nlohmann::json diags = nlohmann::json::array();
    diags.push_back(to_lsp_diagnostic(diag, sources));
    action["diagnostics"] = std::move(diags);

    nlohmann::json changes = nlohmann::json::object();
    changes[std::string{document_uri}] = fix_edits_to_text_edits(fix, sources);
    nlohmann::json workspace_edit = nlohmann::json::object();
    workspace_edit["changes"] = std::move(changes);
    action["edit"] = std::move(workspace_edit);

    action["isPreferred"] = fix.machine_applicable;
    return action;
}

}  // namespace

std::string docs_url_for_rule(std::string_view rule_id) {
    std::string url{k_docs_url_prefix};
    url.append(rule_id);
    url.append(".md");
    return url;
}

nlohmann::json code_actions_for_range(const std::vector<hlsl_clippy::Diagnostic>& diagnostics,
                                      const hlsl_clippy::SourceManager& sources,
                                      std::string_view document_uri,
                                      std::int32_t requested_line_start,
                                      std::int32_t requested_char_start,
                                      std::int32_t requested_line_end,
                                      std::int32_t requested_char_end) {
    nlohmann::json out = nlohmann::json::array();
    const Position request_lo{requested_line_start, requested_char_start};
    const Position request_hi{requested_line_end, requested_char_end};

    for (const auto& diag : diagnostics) {
        if (diag.fixes.empty()) {
            continue;
        }
        const auto diag_lo =
            to_position(sources, diag.primary_span.source, diag.primary_span.bytes.lo);
        const auto diag_hi =
            to_position(sources, diag.primary_span.source, diag.primary_span.bytes.hi);
        if (!ranges_overlap(diag_lo, diag_hi, request_lo, request_hi)) {
            continue;
        }
        for (const auto& fix : diag.fixes) {
            out.push_back(build_code_action(diag, fix, sources, document_uri));
        }
    }
    return out;
}

nlohmann::json hover_for_position(const std::vector<hlsl_clippy::Diagnostic>& diagnostics,
                                  const hlsl_clippy::SourceManager& sources,
                                  std::int32_t line,
                                  std::int32_t character) {
    const Position cursor{line, character};
    // Prefer the smallest-span diagnostic that covers the cursor — this is
    // what users intuitively expect when several diagnostics nest. Pick by
    // primary-span byte length; tie-break by first-seen.
    const hlsl_clippy::Diagnostic* best = nullptr;
    std::uint32_t best_len = 0U;
    for (const auto& diag : diagnostics) {
        const auto lo = to_position(sources, diag.primary_span.source, diag.primary_span.bytes.lo);
        const auto hi = to_position(sources, diag.primary_span.source, diag.primary_span.bytes.hi);
        if (!ranges_overlap(lo, hi, cursor, cursor)) {
            continue;
        }
        const auto len = diag.primary_span.bytes.size();
        if (best == nullptr || len < best_len) {
            best = &diag;
            best_len = len;
        }
    }
    if (best == nullptr) {
        return nlohmann::json{};  // null per LSP §"Hover".
    }

    std::string md;
    md.append("**");
    md.append(best->code);
    md.append("** ([docs](");
    md.append(docs_url_for_rule(best->code));
    md.append("))\n\n");
    md.append(best->message);

    nlohmann::json contents = nlohmann::json::object();
    contents["kind"] = "markdown";
    contents["value"] = std::move(md);
    nlohmann::json out = nlohmann::json::object();
    out["contents"] = std::move(contents);
    return out;
}

}  // namespace hlsl_clippy::lsp::server
