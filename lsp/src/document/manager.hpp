// DocumentManager — open-document registry for the LSP server.
//
// Per ADR 0014 §"Document lifecycle":
//   - Open documents are indexed by URI string.
//   - Each entry tracks { uri, path, content, version, last-lint-time }.
//   - Incremental edits are applied via line/character ranges that LSP
//     ships in didChange notifications.
//   - Re-lint on didChange is debounced via a steady_clock timestamp
//     comparison against a 150 ms threshold (no actual timer; the
//     server gates lint inline based on elapsed time).

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "shader_clippy/diagnostic.hpp"

namespace shader_clippy::lsp::document {

/// One change record from a `textDocument/didChange` notification. LSP 3.x
/// allows either a full-document replace (range absent) or an
/// incremental edit (range present, with start/end in line/character).
struct ContentChange {
    bool has_range = false;
    /// Inclusive start position; `line` and `character` are 0-based per LSP.
    std::uint32_t start_line = 0;
    std::uint32_t start_character = 0;
    /// Exclusive end position.
    std::uint32_t end_line = 0;
    std::uint32_t end_character = 0;
    std::string text;
};

/// One open document.
struct OpenDocument {
    std::string uri;
    std::filesystem::path path;
    std::string contents;
    std::int32_t version = 0;
    std::vector<shader_clippy::Diagnostic> latest_diagnostics;
    std::chrono::steady_clock::time_point last_change_time;
    std::chrono::steady_clock::time_point last_lint_time;
};

/// Registry of currently-open documents.
class DocumentManager {
public:
    DocumentManager() = default;

    /// Open a new document. Replaces any existing entry under the same URI.
    /// Returns a pointer to the registered entry (stable until `close`).
    OpenDocument& open(std::string uri,
                       std::filesystem::path path,
                       std::string contents,
                       std::int32_t version);

    /// Apply an LSP `textDocument/didChange` payload. Returns false when
    /// the URI is not known, or when an incremental edit references a
    /// position outside the current contents.
    [[nodiscard]] bool apply_changes(std::string_view uri,
                                     std::int32_t new_version,
                                     const std::vector<ContentChange>& changes);

    /// Drop a document. No-op when the URI is not known.
    void close(std::string_view uri);

    /// Look up a document. Returns `nullptr` when the URI is not registered.
    [[nodiscard]] OpenDocument* find(std::string_view uri);
    [[nodiscard]] const OpenDocument* find(std::string_view uri) const;

    /// True when the document was changed less than `debounce` ago and
    /// therefore should not be re-linted yet. Returns false when the URI is
    /// unknown (the caller should not lint a missing document anyway).
    [[nodiscard]] bool should_debounce_lint(
        std::string_view uri,
        std::chrono::steady_clock::duration debounce = std::chrono::milliseconds(150)) const;

    /// Mark a document as just-linted. Used by the handler layer to record
    /// when the most recent lint completed.
    void mark_linted(std::string_view uri);

    /// Number of open documents.
    [[nodiscard]] std::size_t size() const noexcept {
        return docs_.size();
    }

    /// True when `uri` is open.
    [[nodiscard]] bool contains(std::string_view uri) const noexcept {
        return docs_.find(std::string{uri}) != docs_.end();
    }

private:
    /// Convert a (line, character) LSP position into a UTF-8 byte offset
    /// into `contents`. Returns `std::nullopt` when the position is out of
    /// bounds.
    [[nodiscard]] static std::optional<std::size_t> position_to_offset(std::string_view contents,
                                                                       std::uint32_t line,
                                                                       std::uint32_t character);

    /// Apply one ContentChange in place. Returns true on success.
    [[nodiscard]] static bool apply_one(std::string& contents, const ContentChange& change);

    std::unordered_map<std::string, OpenDocument> docs_;
};

}  // namespace shader_clippy::lsp::document
