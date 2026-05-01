// file:// URI parsing/encoding implementation.

#include "document/uri.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hlsl_clippy::lsp::document {

namespace {

constexpr std::string_view k_file_scheme = "file://";

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

[[nodiscard]] std::optional<std::string> percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '%') {
            out.push_back(c);
            continue;
        }
        if (i + 2U >= s.size()) {
            return std::nullopt;
        }
        const int hi = hex_value(s[i + 1U]);
        const int lo = hex_value(s[i + 2U]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2U;
    }
    return out;
}

[[nodiscard]] bool is_unreserved(unsigned char c) noexcept {
    // RFC 3986 §2.3 unreserved chars.
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return c == '-' || c == '_' || c == '.' || c == '~';
}

}  // namespace

std::optional<std::filesystem::path> uri_to_path(std::string_view uri) {
    if (!uri.starts_with(k_file_scheme)) {
        return std::nullopt;
    }
    auto remainder = uri.substr(k_file_scheme.size());

    // Optional authority (always empty for file://) — `file:///path` has an
    // empty authority. The leading `/` belongs to the path on POSIX. On
    // Windows, `file:///C:/foo` decodes to `C:/foo` after dropping that
    // leading slash.
    std::string decoded_buf;
    if (auto decoded = percent_decode(remainder); decoded.has_value()) {
        decoded_buf = std::move(*decoded);
    } else {
        return std::nullopt;
    }

    std::string_view decoded = decoded_buf;

#if defined(_WIN32)
    // Windows: `/C:/...` → `C:/...`. The drive-letter check is case-blind.
    if (decoded.size() >= 3U && decoded[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(decoded[1])) != 0 && decoded[2] == ':') {
        decoded.remove_prefix(1U);
    }
#endif

    return std::filesystem::path{std::string{decoded}};
}

std::string path_to_uri(const std::filesystem::path& path) {
    std::string raw = path.generic_string();
#if defined(_WIN32)
    // VS Code expects `file:///C:/foo`, not `file://C:/foo`.
    if (raw.size() >= 2U && raw[1] == ':') {
        raw.insert(raw.begin(), '/');
    }
#endif
    if (raw.empty() || raw.front() != '/') {
        raw.insert(raw.begin(), '/');
    }

    std::string out;
    out.reserve(k_file_scheme.size() + raw.size() + 16U);
    out.append(k_file_scheme);
    for (const char c : raw) {
        const auto uc = static_cast<unsigned char>(c);
        if (is_unreserved(uc) || uc == '/' || uc == ':') {
            out.push_back(c);
        } else {
            constexpr std::string_view k_hex = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(k_hex[(uc >> 4) & 0x0FU]);
            out.push_back(k_hex[uc & 0x0FU]);
        }
    }
    return out;
}

}  // namespace hlsl_clippy::lsp::document
