// JSON-RPC method-name routing.
//
// The dispatcher owns a flat method-name → handler map. Two distinct handler
// shapes:
//
//   - RequestHandler: takes the request `params`, returns either a JSON
//     `result` or an `ErrorObject`. The dispatcher wraps the return value
//     in an LSP-shaped response envelope keyed by the request `id`.
//   - NotificationHandler: takes the notification `params`, returns
//     nothing. The dispatcher emits no response.
//
// Per ADR 0014 §"JSON-RPC layer choice", this is intentionally a small
// in-tree dispatcher, not a third-party framework. The trade-off is that
// we own the handler signatures; the upside is no LSP-framework versioning
// hazard riding alongside Slang's already-difficult ABI.

#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "rpc/message.hpp"

namespace shader_clippy::lsp::rpc {

/// Synchronous request handler. Returns a JSON `result` payload or an
/// `ErrorObject`. The dispatcher wraps the return value in the LSP response
/// envelope with the original request `id`.
using RequestHandler = std::function<std::expected<Json, ErrorObject>(const Json& params)>;

/// Synchronous notification handler. Notifications never produce a response;
/// the return type is `void`.
using NotificationHandler = std::function<void(const Json& params)>;

/// Routes JSON-RPC messages to method-name-keyed handlers. Construction is
/// cheap; handlers are registered once at server bring-up and not mutated
/// afterwards.
class JsonRpcDispatcher {
public:
    /// Register a request handler. Replaces any existing handler bound to
    /// the same method name.
    void on_request(std::string method, RequestHandler handler);

    /// Register a notification handler. Replaces any existing handler.
    void on_notification(std::string method, NotificationHandler handler);

    /// Dispatch a single JSON-RPC message envelope. Returns:
    ///   - A non-empty JSON string when the input was a request — caller
    ///     should write it back to the wire (already serialised; not yet
    ///     framed).
    ///   - An empty string when the input was a notification or when the
    ///     dispatcher decided not to reply (e.g. for an `exit` notification
    ///     after `shutdown`).
    ///
    /// Inputs that are not valid JSON-RPC (missing `jsonrpc`, missing
    /// `method`, ...) produce a synthetic error response when an `id` was
    /// present and an empty string otherwise — never a thrown exception.
    [[nodiscard]] std::string dispatch(const Json& envelope) const;

    /// Convenience: parse `wire_body` as JSON, then call `dispatch`.
    /// Malformed JSON produces a `parse_error` response when it is
    /// recoverable (i.e. when the envelope has an id; otherwise empty).
    [[nodiscard]] std::string dispatch_wire(std::string_view wire_body) const;

    /// Build a serialised error response for the given id + error.
    /// Public so handlers can compose their own.
    [[nodiscard]] static std::string error_response(const Id& id, const ErrorObject& err);

private:
    [[nodiscard]] std::string handle_request(const Request& req) const;
    void handle_notification(const Notification& note) const;

    std::unordered_map<std::string, RequestHandler> request_handlers_;
    std::unordered_map<std::string, NotificationHandler> notification_handlers_;
};

}  // namespace shader_clippy::lsp::rpc
