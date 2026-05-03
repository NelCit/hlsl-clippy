#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shader_clippy {

/// Opaque handle for a source file registered with `SourceManager`.
/// `SourceId{0}` is reserved as the invalid id.
struct SourceId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }
    friend constexpr bool operator==(SourceId lhs, SourceId rhs) noexcept {
        return lhs.value == rhs.value;
    }
    friend constexpr bool operator!=(SourceId lhs, SourceId rhs) noexcept {
        return lhs.value != rhs.value;
    }
};

/// Half-open `[lo, hi)` range expressed in UTF-8 byte offsets into the source
/// buffer. This is the canonical span representation: line/column is computed
/// lazily via `SourceManager::resolve()` for human-readable output.
struct ByteSpan {
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return hi <= lo;
    }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept {
        return hi > lo ? hi - lo : 0U;
    }
};

/// A span paired with the source it points into.
struct Span {
    SourceId source;
    ByteSpan bytes;
};

/// A 1-based `(line, column)` pair derived from a byte offset.
struct LineCol {
    std::uint32_t line = 1;    ///< 1-based line number.
    std::uint32_t column = 1;  ///< 1-based column number, in UTF-8 bytes.
};

/// Owns the canonical UTF-8 buffer and metadata for one source file.
/// Construction is internal to `SourceManager`.
class SourceFile {
public:
    SourceFile(SourceId id, std::filesystem::path path, std::string contents);

    [[nodiscard]] SourceId id() const noexcept {
        return id_;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }
    [[nodiscard]] std::string_view contents() const noexcept {
        return contents_;
    }

    /// Resolve a byte offset into a 1-based `(line, column)`.
    /// Offsets at or beyond `contents().size()` are clamped to end-of-file.
    [[nodiscard]] LineCol line_col(std::uint32_t byte_offset) const noexcept;

    /// Return the substring covering the source line containing `byte_offset`,
    /// stripped of a trailing `\n` (and `\r`). Useful for snippet rendering.
    [[nodiscard]] std::string_view line_text(std::uint32_t byte_offset) const noexcept;

private:
    /// Line-offset table: `line_starts_[i]` is the byte offset of the start of
    /// the (i+1)-th line. `line_starts_[0]` is always 0.
    void build_line_starts();

    SourceId id_;
    std::filesystem::path path_;
    std::string contents_;
    std::vector<std::uint32_t> line_starts_;
};

/// Owns every `SourceFile` for the lifetime of one lint session.
/// Borrows are stable: a `SourceId` returned by `add_*` remains valid for the
/// lifetime of the manager, and references to a `SourceFile` are valid as
/// long as the manager is alive.
class SourceManager {
public:
    SourceManager() = default;
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;
    SourceManager(SourceManager&&) noexcept = default;
    SourceManager& operator=(SourceManager&&) noexcept = default;
    ~SourceManager() = default;

    /// Read `path` from disk and register it. Returns an invalid `SourceId`
    /// (with `valid() == false`) if the file cannot be read.
    [[nodiscard]] SourceId add_file(const std::filesystem::path& path);

    /// Register an in-memory buffer under a synthetic name.
    [[nodiscard]] SourceId add_buffer(const std::string& virtual_path, std::string contents);

    /// Look up a previously-added source. Returns `nullptr` for an invalid id.
    [[nodiscard]] const SourceFile* get(SourceId id) const noexcept;

    /// Convenience: resolve a byte offset directly from a `SourceId`.
    [[nodiscard]] LineCol resolve(SourceId id, std::uint32_t byte_offset) const noexcept;

private:
    // unique_ptr keeps SourceFile addresses stable across reallocation of the
    // backing vector.
    std::vector<std::unique_ptr<SourceFile>> files_;
};

}  // namespace shader_clippy
