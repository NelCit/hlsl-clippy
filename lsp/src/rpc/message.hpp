// Opaque request/response/notification record types for the JSON-RPC
// dispatcher.
//
// LSP carries three distinct message shapes on a single wire:
//   - Request: has `id` (number or string) and `method`. Caller expects a
//     response.
//   - Response: has `id` plus exactly one of `result` / `error`. Sent as a
//     reply to a Request.
//   - Notification: has `method` but no `id`. Caller does not expect a
//     reply.
//
// We model `Id` as a discriminated union of int64 and string. Both forms
// echo back through the response with the same shape they arrived in
// (per the JSON-RPC 2.0 spec).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

namespace hlsl_clippy::lsp::rpc {

using Json = nlohmann::json;

/// A JSON-RPC request id. The spec admits null but VS Code never sends one,
/// and we interpret a missing id as a notification anyway.
using Id = std::variant<std::int64_t, std::string>;

/// Parsed request envelope. `params` may be null/missing.
struct Request {
    Id id;
    std::string method;
    Json params;
};

/// Parsed notification envelope (no id; no response expected).
struct Notification {
    std::string method;
    Json params;
};

/// Response error payload (per JSON-RPC 2.0 §5.1).
struct ErrorObject {
    std::int32_t code = 0;
    std::string message;
    Json data;  ///< optional, may be null.
};

/// Standard JSON-RPC + LSP error codes. Values come from
/// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#errorCodes
namespace error_code {
constexpr std::int32_t k_parse_error = -32700;
constexpr std::int32_t k_invalid_request = -32600;
constexpr std::int32_t k_method_not_found = -32601;
constexpr std::int32_t k_invalid_params = -32602;
constexpr std::int32_t k_internal_error = -32603;
constexpr std::int32_t k_server_not_initialized = -32002;
constexpr std::int32_t k_request_failed = -32803;
}  // namespace error_code

/// Build the LSP id sub-object as JSON. Used by both the dispatcher (for
/// success responses) and error-reply construction.
[[nodiscard]] inline Json id_to_json(const Id& id) {
    return std::visit([](const auto& v) -> Json { return Json(v); }, id);
}

}  // namespace hlsl_clippy::lsp::rpc
