// file:// URI parsing and encoding.
//
// LSP identifies documents by URI string. The most common form is
// `file:///<path>` on POSIX and `file:///C:/<path>` on Windows. We need to
// decode percent-escapes and convert the URI back into a `std::filesystem::
// path` that `core` can lint against.
//
// This is deliberately not a full RFC 3986 parser — LSP's slice of URI
// usage is narrow. Anything other than `file://` is rejected.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace shader_clippy::lsp::document {

/// Parse a `file://` URI into a filesystem path. Returns `std::nullopt` for
/// unsupported schemes or malformed encodings.
[[nodiscard]] std::optional<std::filesystem::path> uri_to_path(std::string_view uri);

/// Encode a filesystem path as a `file://` URI. The returned string is
/// percent-encoded per RFC 3986 §3.3 with the characters that LSP / VS Code
/// expect.
[[nodiscard]] std::string path_to_uri(const std::filesystem::path& path);

}  // namespace shader_clippy::lsp::document
