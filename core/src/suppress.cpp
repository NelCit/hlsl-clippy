// Inline-suppression scanner for `// shader-clippy: allow(rule-name, ...)` and
// `// shader-clippy: allow(*)` markers.
//
// The scanner is intentionally grammar-agnostic: it walks the raw UTF-8 bytes
// with a flat dispatch loop (line comments, block comments, string literals,
// or plain code) so that it never depends on a tree-sitter parse. Suppression
// must work even when the grammar produces ERROR nodes.

#include "shader_clippy/suppress.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shader_clippy {

namespace {

constexpr std::string_view k_marker = "shader-clippy:";
constexpr std::string_view k_allow = "allow";
constexpr std::string_view k_wildcard_rule = "*";

[[nodiscard]] bool is_space_or_tab(char c) noexcept {
    return c == ' ' || c == '\t';
}

[[nodiscard]] bool is_ident_char(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    // The colon is part of the `clippy::*` infrastructure-rule namespace
    // (e.g. `clippy::language-skip-ast`, `clippy::cfg-skip`,
    // `clippy::reflection`). Accepting it here lets users inline-suppress
    // the engine notices the same way they suppress regular rules.
    return c == '-' || c == '_' || c == '*' || c == ':' || (std::isalnum(u) != 0);
}

/// Skip ASCII spaces and tabs (not newlines), returning the new index.
[[nodiscard]] std::size_t skip_inline_ws(std::string_view src, std::size_t i) noexcept {
    while (i < src.size() && is_space_or_tab(src[i])) {
        ++i;
    }
    return i;
}

/// Skip an ASCII identifier-ish run, returning the new index.
[[nodiscard]] std::size_t skip_ident(std::string_view src, std::size_t i) noexcept {
    while (i < src.size() && is_ident_char(src[i])) {
        ++i;
    }
    return i;
}

/// Returns true and `out_consumed` advance length if `src.substr(i)` starts
/// with `needle` (case-sensitive).
[[nodiscard]] bool starts_with(std::string_view src,
                               std::size_t i,
                               std::string_view needle) noexcept {
    if (i + needle.size() > src.size()) {
        return false;
    }
    return src.compare(i, needle.size(), needle) == 0;
}

/// Parsed suppression-marker payload: rule names and the byte range of the
/// comment text itself (used for diagnostics about malformed markers).
struct ParsedMarker {
    std::vector<std::string> rules;
    std::uint32_t marker_byte_lo = 0;
    std::uint32_t marker_byte_hi = 0;
    bool well_formed = true;
};

/// Parse the body of one `// shader-clippy:` comment, given indices into the
/// surrounding source. `comment_body_lo` is the first byte after `//`,
/// `comment_body_hi` is one past the last byte before the end-of-line.
[[nodiscard]] ParsedMarker parse_marker(std::string_view src,
                                        std::size_t comment_body_lo,
                                        std::size_t comment_body_hi) {
    ParsedMarker out;
    out.marker_byte_lo = static_cast<std::uint32_t>(comment_body_lo);
    out.marker_byte_hi = static_cast<std::uint32_t>(comment_body_hi);

    std::size_t i = skip_inline_ws(src, comment_body_lo);
    if (!starts_with(src, i, k_marker)) {
        out.well_formed = false;
        return out;
    }
    i += k_marker.size();
    i = skip_inline_ws(src, i);

    if (!starts_with(src, i, k_allow)) {
        out.well_formed = false;
        return out;
    }
    i += k_allow.size();
    i = skip_inline_ws(src, i);

    if (i >= comment_body_hi || src[i] != '(') {
        out.well_formed = false;
        return out;
    }
    ++i;  // past '('

    while (i < comment_body_hi) {
        i = skip_inline_ws(src, i);
        if (i < comment_body_hi && src[i] == ')') {
            ++i;
            return out;
        }
        const std::size_t name_lo = i;
        i = skip_ident(src, i);
        if (i == name_lo) {
            out.well_formed = false;
            return out;
        }
        out.rules.emplace_back(src.substr(name_lo, i - name_lo));
        i = skip_inline_ws(src, i);
        if (i < comment_body_hi && src[i] == ',') {
            ++i;
            continue;
        }
        if (i < comment_body_hi && src[i] == ')') {
            ++i;
            return out;
        }
        out.well_formed = false;
        return out;
    }

    out.well_formed = false;
    return out;
}

/// Return one past the end-of-line for the comment that starts at index `i`.
/// `i` should point to the first byte after the leading `//`.
[[nodiscard]] std::size_t end_of_line(std::string_view src, std::size_t i) noexcept {
    while (i < src.size() && src[i] != '\n') {
        ++i;
    }
    return i;
}

/// Return one past the matching `*/` for a block comment. `i` points after `/*`.
[[nodiscard]] std::size_t end_of_block(std::string_view src, std::size_t i) noexcept {
    while (i + 1U < src.size()) {
        if (src[i] == '*' && src[i + 1U] == '/') {
            return i + 2U;
        }
        ++i;
    }
    return src.size();
}

/// Skip a string literal opened at `i` with the given quote character.
/// Returns one past the closing quote, or `src.size()` if unterminated.
/// Honours C-style backslash escapes inside the literal.
[[nodiscard]] std::size_t skip_string_literal(std::string_view src,
                                              std::size_t i,
                                              char quote) noexcept {
    ++i;  // past opening quote
    while (i < src.size() && src[i] != quote) {
        if (src[i] == '\\' && i + 1U < src.size()) {
            i += 2U;
            continue;
        }
        ++i;
    }
    if (i < src.size()) {
        ++i;
    }
    return i;
}

/// Find the first non-whitespace, non-comment byte at or after `i`. Returns
/// `src.size()` if EOF is reached. Tracks comments so it can step over them.
[[nodiscard]] std::size_t skip_ws_and_comments(std::string_view src, std::size_t i) noexcept {
    while (i < src.size()) {
        const char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1U < src.size()) {
            if (src[i + 1U] == '/') {
                i = end_of_line(src, i + 2U);
                continue;
            }
            if (src[i + 1U] == '*') {
                i = end_of_block(src, i + 2U);
                continue;
            }
        }
        return i;
    }
    return src.size();
}

/// Step past one comment or string-literal token starting at `i`. Returns the
/// new index, or `i` unchanged if the byte is plain code.
[[nodiscard]] std::size_t skip_token(std::string_view src, std::size_t i) noexcept {
    const char c = src[i];
    if (c == '/' && i + 1U < src.size() && src[i + 1U] == '/') {
        return end_of_line(src, i + 2U);
    }
    if (c == '/' && i + 1U < src.size() && src[i + 1U] == '*') {
        return end_of_block(src, i + 2U);
    }
    if (c == '"' || c == '\'') {
        return skip_string_literal(src, i, c);
    }
    return i;
}

/// Find one past the matching `}` for a `{` at index `open_brace`. Tracks
/// nested braces, comments, and string literals so embedded `}` characters
/// don't terminate the scope early.
[[nodiscard]] std::size_t end_of_block_brace(std::string_view src,
                                             std::size_t open_brace) noexcept {
    if (open_brace >= src.size() || src[open_brace] != '{') {
        return src.size();
    }
    int depth = 1;
    std::size_t i = open_brace + 1U;
    while (i < src.size() && depth > 0) {
        const std::size_t after_token = skip_token(src, i);
        if (after_token != i) {
            i = after_token;
            continue;
        }
        const char c = src[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return i + 1U;
            }
        }
        ++i;
    }
    return src.size();
}

/// One past the start of the next non-comment, non-blank line.
/// `line_end` should point at the `\n` (or EOF) that ends the comment line.
[[nodiscard]] std::uint32_t next_code_line_end(std::string_view src,
                                               std::size_t line_end) noexcept {
    std::size_t i = line_end;
    if (i < src.size() && src[i] == '\n') {
        ++i;
    }
    // Skip blank / comment-only lines.
    while (i < src.size()) {
        // Skip leading whitespace on this line.
        while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) {
            ++i;
        }
        if (i < src.size() && src[i] == '\n') {
            // Blank line.
            ++i;
            continue;
        }
        if (i + 1U < src.size() && src[i] == '/' && src[i + 1U] == '/') {
            // Line comment — step over and continue.
            i = end_of_line(src, i + 2U);
            if (i < src.size() && src[i] == '\n') {
                ++i;
            }
            continue;
        }
        if (i + 1U < src.size() && src[i] == '/' && src[i + 1U] == '*') {
            i = end_of_block(src, i + 2U);
            continue;
        }
        // First real code token of the next code line.
        // Scope = the entire line containing this token.
        const std::size_t eol = end_of_line(src, i);
        return static_cast<std::uint32_t>(eol);
    }
    return static_cast<std::uint32_t>(src.size());
}

/// True if any byte between `[lo, hi)` in `src` is non-whitespace. Used to
/// distinguish a leading-line comment from a trailing-line comment.
[[nodiscard]] bool any_non_ws(std::string_view src, std::size_t lo, std::size_t hi) noexcept {
    for (std::size_t k = lo; k < hi && k < src.size(); ++k) {
        const char kc = src[k];
        if (kc != ' ' && kc != '\t') {
            return true;
        }
    }
    return false;
}

/// True if any element of `rules` is the wildcard `"*"`.
[[nodiscard]] bool contains_wildcard(const std::vector<std::string>& rules) noexcept {
    return std::ranges::any_of(rules,
                               [](const std::string& r) noexcept { return r == k_wildcard_rule; });
}

/// One past the start of the next non-comment, non-blank line. Returns the
/// byte offset of that line's first code character.
[[nodiscard]] std::uint32_t next_code_line_start(std::string_view src,
                                                 std::size_t line_end) noexcept {
    std::size_t i = line_end;
    if (i < src.size() && src[i] == '\n') {
        ++i;
    }
    while (i < src.size()) {
        const std::size_t line_start_save = i;
        while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) {
            ++i;
        }
        if (i < src.size() && src[i] == '\n') {
            ++i;
            continue;
        }
        if (i + 1U < src.size() && src[i] == '/' && src[i + 1U] == '/') {
            i = end_of_line(src, i + 2U);
            if (i < src.size() && src[i] == '\n') {
                ++i;
            }
            continue;
        }
        if (i + 1U < src.size() && src[i] == '/' && src[i + 1U] == '*') {
            i = end_of_block(src, i + 2U);
            continue;
        }
        return static_cast<std::uint32_t>(line_start_save);
    }
    return static_cast<std::uint32_t>(src.size());
}

/// Resolved scope for a well-formed marker. `byte_lo`/`byte_hi` are the byte
/// range to which the suppression applies; `wildcard_at_top` signals that the
/// marker should expand to the entire file (only true for `allow(*)` markers
/// that appear before any code).
struct ResolvedScope {
    std::uint32_t byte_lo = 0;
    std::uint32_t byte_hi = 0;
    bool wildcard_at_top = false;
};

/// Compute the suppression scope for a marker that ended at byte
/// `comment_hi` (exclusive). The marker started at byte
/// `line_comment_lo - 2` (the `//`).
[[nodiscard]] ResolvedScope resolve_scope(std::string_view source,
                                          std::size_t line_comment_lo,
                                          std::size_t comment_hi,
                                          bool seen_real_code,
                                          const std::vector<std::string>& rules) noexcept {
    ResolvedScope rs;

    // Find start of this comment's line.
    std::size_t line_start = line_comment_lo >= 2U ? line_comment_lo - 2U : 0;
    while (line_start > 0 && source[line_start - 1U] != '\n') {
        --line_start;
    }
    const std::size_t comment_start = line_comment_lo >= 2U ? line_comment_lo - 2U : 0;
    const bool same_line_code = any_non_ws(source, line_start, comment_start);

    if (same_line_code) {
        // Trailing comment on a code line: scope to that line.
        rs.byte_lo = static_cast<std::uint32_t>(line_start);
        rs.byte_hi = static_cast<std::uint32_t>(comment_hi);
        return rs;
    }

    // Find the next non-comment, non-whitespace byte.
    const std::size_t next_token_idx = skip_ws_and_comments(source, comment_hi);
    if (next_token_idx >= source.size()) {
        rs.byte_lo = static_cast<std::uint32_t>(comment_hi);
        rs.byte_hi = rs.byte_lo;
    } else if (source[next_token_idx] == '{') {
        rs.byte_lo = static_cast<std::uint32_t>(next_token_idx);
        rs.byte_hi = static_cast<std::uint32_t>(end_of_block_brace(source, next_token_idx));
    } else {
        rs.byte_lo = next_code_line_start(source, comment_hi);
        rs.byte_hi = next_code_line_end(source, comment_hi);
    }

    // File-scope: `allow(*)` at top of file (no real code seen yet) covers
    // the whole file.
    if (!seen_real_code && contains_wildcard(rules)) {
        rs.wildcard_at_top = true;
    }
    return rs;
}

/// Build entries for a successfully-parsed marker and return them. The caller
/// appends them to its `SuppressionSet`. The by-value `rules` parameter is
/// moved into per-entry rule ids when the marker is not a top-of-file
/// wildcard.
[[nodiscard]] std::vector<Suppression> build_entries(const ResolvedScope& rs,
                                                     std::string_view source,
                                                     std::vector<std::string> rules) {
    std::vector<Suppression> out;
    if (rs.wildcard_at_top) {
        Suppression s;
        s.rule_id = std::string{k_wildcard_rule};
        s.byte_lo = 0;
        s.byte_hi = static_cast<std::uint32_t>(source.size());
        out.push_back(std::move(s));
        return out;
    }
    out.reserve(rules.size());
    for (auto& r : rules) {
        Suppression s;
        s.rule_id = std::move(r);
        s.byte_lo = rs.byte_lo;
        s.byte_hi = rs.byte_hi;
        out.push_back(std::move(s));
    }
    return out;
}

/// Build a malformed-marker scan diagnostic if the comment body started with
/// the `shader-clippy:` trigger. Returns `std::nullopt` otherwise.
[[nodiscard]] std::optional<SuppressionSet::ScanDiagnostic> build_malformed(
    std::string_view source, std::size_t line_comment_lo, std::size_t comment_hi) noexcept {
    const std::size_t probe = skip_inline_ws(source, line_comment_lo);
    if (!starts_with(source, probe, k_marker)) {
        return std::nullopt;
    }
    SuppressionSet::ScanDiagnostic d;
    d.message =
        std::string{"malformed `// shader-clippy:` annotation; expected `allow(rule-name, ...)`"};
    d.byte_lo = static_cast<std::uint32_t>(line_comment_lo - 2U);
    d.byte_hi = static_cast<std::uint32_t>(comment_hi);
    return d;
}

/// Process the body of a single line comment that just terminated. Either
/// appends one or more `Suppression` entries to `entries_out` (well-formed
/// marker), or appends a scan diagnostic to `diags_out` (malformed marker),
/// or does nothing (the comment isn't a shader-clippy marker at all).
void finalize_line_comment(std::string_view source,
                           std::size_t line_comment_lo,
                           std::size_t comment_hi,
                           bool seen_real_code,
                           std::vector<Suppression>& entries_out,
                           std::vector<SuppressionSet::ScanDiagnostic>& diags_out) {
    auto marker = parse_marker(source, line_comment_lo, comment_hi);
    if (marker.well_formed && !marker.rules.empty()) {
        const ResolvedScope rs =
            resolve_scope(source, line_comment_lo, comment_hi, seen_real_code, marker.rules);
        auto entries = build_entries(rs, source, std::move(marker.rules));
        for (auto& e : entries) {
            entries_out.push_back(std::move(e));
        }
        return;
    }
    if (auto d = build_malformed(source, line_comment_lo, comment_hi); d.has_value()) {
        diags_out.push_back(std::move(*d));
    }
}

}  // namespace

SuppressionSet SuppressionSet::scan(std::string_view source) {
    SuppressionSet out;
    bool seen_real_code = false;
    std::size_t i = 0;

    while (i < source.size()) {
        const char c = source[i];

        // Line comment: capture the body, dispatch the marker handler, resume
        // at the end-of-line. The body excludes the leading `//`.
        if (c == '/' && i + 1U < source.size() && source[i + 1U] == '/') {
            const std::size_t body_lo = i + 2U;
            const std::size_t body_hi = end_of_line(source, body_lo);
            finalize_line_comment(
                source, body_lo, body_hi, seen_real_code, out.entries_, out.scan_diagnostics_);
            i = body_hi;  // Loop body will step over the trailing newline.
            continue;
        }

        // Block comment: skip without disturbing `seen_real_code`. A block
        // comment at the top of file does not delay file-scope suppression.
        if (c == '/' && i + 1U < source.size() && source[i + 1U] == '*') {
            i = end_of_block(source, i + 2U);
            continue;
        }

        // String literal: skip past the closing quote. Marker text inside a
        // literal is intentionally ignored.
        if (c == '"' || c == '\'') {
            seen_real_code = true;
            i = skip_string_literal(source, i, c);
            continue;
        }

        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            seen_real_code = true;
        }
        ++i;
    }

    out.index();
    return out;
}

void SuppressionSet::index() {
    by_rule_.clear();
    wildcard_.clear();
    for (const auto& e : entries_) {
        if (e.byte_hi <= e.byte_lo) {
            continue;
        }
        if (e.rule_id == k_wildcard_rule) {
            wildcard_.emplace_back(e.byte_lo, e.byte_hi);
        } else {
            by_rule_[e.rule_id].emplace_back(e.byte_lo, e.byte_hi);
        }
    }
    auto sort_intervals = [](std::vector<std::pair<std::uint32_t, std::uint32_t>>& v) {
        std::ranges::sort(v,
                          [](const auto& a, const auto& b) noexcept { return a.first < b.first; });
    };
    sort_intervals(wildcard_);
    for (auto& [_, list] : by_rule_) {
        sort_intervals(list);
    }
}

bool SuppressionSet::suppresses(std::string_view rule_id,
                                std::uint32_t byte_lo,
                                std::uint32_t byte_hi) const noexcept {
    auto covers = [&](const std::vector<std::pair<std::uint32_t, std::uint32_t>>& list) noexcept {
        // A suppression covers a diagnostic if the diagnostic's start byte
        // falls inside the suppression range. We match on the start byte
        // rather than full overlap so that long diagnostics anchored to a
        // specific line are not silently suppressed by a tangentially
        // overlapping range. The defensive empty-span clause ensures that
        // zero-byte diagnostics still hit ranges they touch at the boundary.
        return std::ranges::any_of(list,
                                   [&](const std::pair<std::uint32_t, std::uint32_t>& iv) noexcept {
                                       const auto lo = iv.first;
                                       const auto hi = iv.second;
                                       if (byte_lo >= lo && byte_lo < hi) {
                                           return true;
                                       }
                                       return byte_lo == byte_hi && byte_lo >= lo && byte_lo <= hi;
                                   });
    };

    if (covers(wildcard_)) {
        return true;
    }
    auto it = by_rule_.find(std::string{rule_id});
    if (it == by_rule_.end()) {
        return false;
    }
    return covers(it->second);
}

}  // namespace shader_clippy
