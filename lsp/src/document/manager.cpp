// DocumentManager implementation.

#include "document/manager.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hlsl_clippy::lsp::document {

namespace {

/// Find the byte offset of the start of line `target_line` (0-based).
/// Returns `contents.size()` when `target_line == count_of_lines` (i.e.
/// position past the trailing newline).
[[nodiscard]] std::optional<std::size_t> line_start_offset(std::string_view contents,
                                                           std::uint32_t target_line) {
    if (target_line == 0U) {
        return std::size_t{0};
    }
    std::uint32_t current_line = 0U;
    for (std::size_t i = 0; i < contents.size(); ++i) {
        if (contents[i] == '\n') {
            ++current_line;
            if (current_line == target_line) {
                return i + 1U;
            }
        }
    }
    if (current_line == target_line) {
        // target line is the synthetic line past the last `\n`.
        return contents.size();
    }
    if (current_line + 1U == target_line) {
        // The buffer may end without a trailing newline; treat the
        // implicit final-line position as valid.
        return contents.size();
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::size_t> DocumentManager::position_to_offset(std::string_view contents,
                                                               std::uint32_t line,
                                                               std::uint32_t character) {
    const auto line_start = line_start_offset(contents, line);
    if (!line_start.has_value()) {
        return std::nullopt;
    }
    // LSP "character" is a UTF-16 code-unit offset by default, but VS Code
    // and almost every other client send UTF-8 byte offsets when the server
    // declares `positionEncoding: "utf-8"` in the initialize response.
    // Sub-phase 5a does NOT advertise that capability, so strictly we should
    // be doing UTF-16 code-unit translation. Pure-ASCII shaders are
    // identical under both encodings; we accept the simplification as a
    // tracked follow-up for sub-phase 5b once code-action edits round-trip
    // through real UTF-16 sources.
    const std::size_t base = *line_start;
    if (base + character > contents.size()) {
        return std::nullopt;
    }
    // Walk forward `character` bytes, but stop at the next newline so a
    // bogus column past EOL clamps cleanly rather than spilling over.
    std::size_t offset = base;
    std::uint32_t consumed = 0U;
    while (consumed < character && offset < contents.size() && contents[offset] != '\n') {
        ++offset;
        ++consumed;
    }
    if (consumed != character) {
        // Position points past EOL; that is permitted by LSP for a range
        // end-position (it means "to end of line").
        return offset;
    }
    return offset;
}

bool DocumentManager::apply_one(std::string& contents, const ContentChange& change) {
    if (!change.has_range) {
        contents = change.text;
        return true;
    }
    const auto lo = position_to_offset(contents, change.start_line, change.start_character);
    const auto hi = position_to_offset(contents, change.end_line, change.end_character);
    if (!lo.has_value() || !hi.has_value() || *lo > *hi) {
        return false;
    }
    contents.replace(*lo, *hi - *lo, change.text);
    return true;
}

OpenDocument& DocumentManager::open(std::string uri,
                                    std::filesystem::path path,
                                    std::string contents,
                                    std::int32_t version) {
    OpenDocument doc;
    doc.uri = uri;
    doc.path = std::move(path);
    doc.contents = std::move(contents);
    doc.version = version;
    doc.last_change_time = std::chrono::steady_clock::now();
    const auto result = docs_.insert_or_assign(std::move(uri), std::move(doc));
    return result.first->second;
}

bool DocumentManager::apply_changes(std::string_view uri,
                                    std::int32_t new_version,
                                    const std::vector<ContentChange>& changes) {
    auto it = docs_.find(std::string{uri});
    if (it == docs_.end()) {
        return false;
    }
    auto& doc = it->second;
    for (const auto& change : changes) {
        if (!apply_one(doc.contents, change)) {
            return false;
        }
    }
    doc.version = new_version;
    doc.last_change_time = std::chrono::steady_clock::now();
    return true;
}

void DocumentManager::close(std::string_view uri) {
    docs_.erase(std::string{uri});
}

OpenDocument* DocumentManager::find(std::string_view uri) {
    auto it = docs_.find(std::string{uri});
    return it == docs_.end() ? nullptr : &it->second;
}

const OpenDocument* DocumentManager::find(std::string_view uri) const {
    auto it = docs_.find(std::string{uri});
    return it == docs_.end() ? nullptr : &it->second;
}

bool DocumentManager::should_debounce_lint(std::string_view uri,
                                           std::chrono::steady_clock::duration debounce) const {
    const auto* doc = find(uri);
    if (doc == nullptr) {
        return false;
    }
    if (doc->last_lint_time == std::chrono::steady_clock::time_point{}) {
        // Never linted — never debounce.
        return false;
    }
    const auto since_change = std::chrono::steady_clock::now() - doc->last_change_time;
    return since_change < debounce;
}

void DocumentManager::mark_linted(std::string_view uri) {
    if (auto* doc = find(uri); doc != nullptr) {
        doc->last_lint_time = std::chrono::steady_clock::now();
    }
}

}  // namespace hlsl_clippy::lsp::document
