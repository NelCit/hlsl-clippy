// LSP base-protocol framing implementation.
//
// The wire format is line-oriented up to a CRLFCRLF terminator, then a
// Content-Length-byte body. We deliberately do not blindly trust the
// stream: malformed Content-Length values are reported as
// `FramingStatus::HeaderError` rather than thrown, in keeping with ADR 0014's
// no-exceptions-across-LSP-boundaries rule.

#include "rpc/framing.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <istream>
#include <string>
#include <string_view>
#include <system_error>

namespace hlsl_clippy::lsp::rpc {

namespace {

constexpr std::string_view k_content_length = "Content-Length";
constexpr std::string_view k_crlf = "\r\n";
constexpr std::string_view k_header_terminator = "\r\n\r\n";

/// Trim ASCII whitespace from both ends of `s`.
[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

/// Case-insensitive ASCII equality. Header names in LSP are conventionally
/// `Content-Length` / `Content-Type` but the protocol does not require an
/// exact case match — accept any casing.
[[nodiscard]] bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<std::uint32_t> parse_content_length(std::string_view headers) {
    std::optional<std::uint32_t> length;
    std::size_t pos = 0;
    while (pos < headers.size()) {
        const auto eol = headers.find(k_crlf, pos);
        const std::string_view line =
            headers.substr(pos, (eol == std::string_view::npos) ? headers.size() - pos : eol - pos);
        pos = (eol == std::string_view::npos) ? headers.size() : eol + k_crlf.size();

        if (line.empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        const auto name = trim(line.substr(0, colon));
        const auto value = trim(line.substr(colon + 1));
        if (ascii_iequals(name, k_content_length)) {
            std::uint32_t parsed = 0;
            const auto* first = value.data();
            const auto* last = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(first, last, parsed);
            if (ec != std::errc{} || ptr != last) {
                return std::nullopt;
            }
            length = parsed;
        }
        // Other headers (Content-Type, etc.) are silently ignored — LSP
        // implementations historically accept anything they don't recognise.
    }
    return length;
}

ReadResult read_message(std::istream& in) {
    ReadResult result;

    // Read header bytes one at a time until we see `\r\n\r\n`. The header is
    // bounded in practice to <1 KiB; cap at 64 KiB to avoid pathological
    // input keeping us reading forever.
    constexpr std::size_t k_max_header_bytes = 65536U;
    std::string headers;
    headers.reserve(256);

    while (true) {
        const auto byte = in.get();
        if (byte == std::istream::traits_type::eof()) {
            // Clean EOS only counts when no header bytes have been seen.
            if (headers.empty()) {
                result.status = FramingStatus::EndOfStream;
            } else {
                result.status = FramingStatus::HeaderError;
            }
            return result;
        }
        headers.push_back(static_cast<char>(byte));
        if (headers.size() >= k_header_terminator.size() &&
            std::string_view(headers).ends_with(k_header_terminator)) {
            break;
        }
        if (headers.size() > k_max_header_bytes) {
            result.status = FramingStatus::HeaderError;
            return result;
        }
    }

    // Strip the trailing `\r\n\r\n` before parsing.
    const std::string_view header_view(headers.data(), headers.size() - k_header_terminator.size());
    const auto length_opt = parse_content_length(header_view);
    if (!length_opt.has_value()) {
        result.status = FramingStatus::HeaderError;
        return result;
    }
    const std::uint32_t length = *length_opt;

    // Cap body size at 16 MiB. LSP requests are small (typically <1 MiB even
    // for large didChange events). A misbehaving or malicious peer sending
    // `Content-Length: 4000000000` over stdin would otherwise allocate
    // multiple GB. Reject as a header error so the dispatcher can recover
    // (the read loop continues with the next message).
    constexpr std::uint32_t k_max_body_bytes = 16U * 1024U * 1024U;
    if (length > k_max_body_bytes) {
        result.status = FramingStatus::HeaderError;
        return result;
    }

    result.body.resize(length);
    if (length > 0U) {
        in.read(result.body.data(), static_cast<std::streamsize>(length));
        const auto got = static_cast<std::uint32_t>(in.gcount());
        if (got != length) {
            result.body.resize(got);
            result.status = FramingStatus::BodyTruncated;
            return result;
        }
    }

    result.status = FramingStatus::Ok;
    return result;
}

std::string frame_message(std::string_view body) {
    std::string out;
    out.reserve(body.size() + 64U);
    out.append(k_content_length);
    out.append(": ");
    out.append(std::to_string(body.size()));
    out.append(k_crlf);
    out.append(k_crlf);
    out.append(body);
    return out;
}

}  // namespace hlsl_clippy::lsp::rpc
