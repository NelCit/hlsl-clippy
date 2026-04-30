#include "hlsl_clippy/source.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hlsl_clippy {

namespace {

[[nodiscard]] std::string read_file_to_string(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

}  // namespace

// ---------------------------------------------------------------------------
// SourceFile
// ---------------------------------------------------------------------------

SourceFile::SourceFile(SourceId id, std::filesystem::path path, std::string contents)
    : id_(id), path_(std::move(path)), contents_(std::move(contents)) {
    build_line_starts();
}

void SourceFile::build_line_starts() {
    line_starts_.clear();
    line_starts_.push_back(0U);
    for (std::size_t i = 0; i < contents_.size(); ++i) {
        if (contents_[i] == '\n') {
            // Next line starts immediately after the newline.
            line_starts_.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }
}

LineCol SourceFile::line_col(std::uint32_t byte_offset) const noexcept {
    if (line_starts_.empty()) {
        return LineCol{.line = 1, .column = 1};
    }
    const auto clamped =
        std::min<std::uint32_t>(byte_offset, static_cast<std::uint32_t>(contents_.size()));

    // upper_bound returns the first element strictly greater than `clamped`.
    // The line start we want is the one just before that.
    const auto it = std::ranges::upper_bound(line_starts_, clamped);
    if (it == line_starts_.begin()) {
        return LineCol{.line = 1, .column = 1};
    }
    const auto prev = std::prev(it);
    const auto line_index = static_cast<std::uint32_t>(std::distance(line_starts_.begin(), prev));
    const std::uint32_t column = clamped - *prev;
    return LineCol{.line = line_index + 1U, .column = column + 1U};
}

std::string_view SourceFile::line_text(std::uint32_t byte_offset) const noexcept {
    if (line_starts_.empty() || contents_.empty()) {
        return {};
    }
    const auto clamped =
        std::min<std::uint32_t>(byte_offset, static_cast<std::uint32_t>(contents_.size()));
    const auto it = std::ranges::upper_bound(line_starts_, clamped);
    const auto line_start = *std::prev(it);
    auto next_start = static_cast<std::uint32_t>(contents_.size());
    if (it != line_starts_.end()) {
        next_start = *it;
    }
    if (next_start <= line_start) {
        return {};
    }
    std::uint32_t line_end = next_start;
    if (line_end > 0U && line_end - 1U < contents_.size() && contents_[line_end - 1U] == '\n') {
        --line_end;
    }
    if (line_end > 0U && line_end - 1U < contents_.size() && contents_[line_end - 1U] == '\r') {
        --line_end;
    }
    if (line_end <= line_start) {
        return {};
    }
    return std::string_view{contents_}.substr(line_start, line_end - line_start);
}

// ---------------------------------------------------------------------------
// SourceManager
// ---------------------------------------------------------------------------

SourceId SourceManager::add_file(const std::filesystem::path& path) {
    std::string contents = read_file_to_string(path);
    if (contents.empty() && !std::filesystem::exists(path)) {
        return SourceId{};
    }
    const auto next_index = files_.size();
    const SourceId id{static_cast<std::uint32_t>(next_index + 1U)};
    files_.push_back(std::make_unique<SourceFile>(id, path, std::move(contents)));
    return id;
}

SourceId SourceManager::add_buffer(const std::string& virtual_path, std::string contents) {
    const auto next_index = files_.size();
    const SourceId id{static_cast<std::uint32_t>(next_index + 1U)};
    files_.push_back(
        std::make_unique<SourceFile>(id, std::filesystem::path{virtual_path}, std::move(contents)));
    return id;
}

const SourceFile* SourceManager::get(SourceId id) const noexcept {
    if (!id.valid()) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(id.value - 1U);
    if (index >= files_.size()) {
        return nullptr;
    }
    return files_[index].get();
}

LineCol SourceManager::resolve(SourceId id, std::uint32_t byte_offset) const noexcept {
    if (const SourceFile* file = get(id); file != nullptr) {
        return file->line_col(byte_offset);
    }
    return LineCol{.line = 1, .column = 1};
}

}  // namespace hlsl_clippy
