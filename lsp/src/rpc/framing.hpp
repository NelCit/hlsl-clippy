// LSP base-protocol framing.
//
// LSP messages are framed with HTTP-like headers terminated by a CRLFCRLF
// sequence, followed by exactly Content-Length bytes of UTF-8 JSON body.
// Per ADR 0014 §"JSON-RPC layer choice", we own the framing; this module is
// the entire surface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace shader_clippy::lsp::rpc {

/// Result of a single framing read attempt against the input stream.
enum class FramingStatus {
    Ok,             ///< Body is in `body`; ready to dispatch.
    EndOfStream,    ///< Stream cleanly closed before any header bytes arrived.
    HeaderError,    ///< Malformed header (missing Content-Length, junk bytes, ...).
    BodyTruncated,  ///< Stream ended mid-body.
};

struct ReadResult {
    FramingStatus status = FramingStatus::EndOfStream;
    std::string body;  ///< Valid only when `status == FramingStatus::Ok`.
};

/// Read one framed LSP message from `in`. Blocks until the body is fully
/// available, the stream closes, or a framing error is detected.
[[nodiscard]] ReadResult read_message(std::istream& in);

/// Encode `body` with a Content-Length header (and the standard `\r\n\r\n`
/// terminator) and return the wire-format bytes ready to write. The caller
/// is responsible for `flush()`-ing the output stream after writing.
[[nodiscard]] std::string frame_message(std::string_view body);

/// Parse a header buffer (everything up to but not including the `\r\n\r\n`
/// terminator). Returns the Content-Length value on success and
/// `std::nullopt` on malformed input. Public for unit-testability.
[[nodiscard]] std::optional<std::uint32_t> parse_content_length(std::string_view headers);

}  // namespace shader_clippy::lsp::rpc
