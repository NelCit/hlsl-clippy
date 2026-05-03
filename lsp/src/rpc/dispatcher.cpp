// JSON-RPC dispatcher implementation.

#include "rpc/dispatcher.hpp"

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "rpc/message.hpp"

namespace shader_clippy::lsp::rpc {

namespace {

/// Parse the `id` field from a JSON envelope. Returns `std::nullopt` when
/// the field is absent or null (i.e. the message is a notification).
[[nodiscard]] std::optional<Id> parse_id(const Json& envelope) {
    const auto it = envelope.find("id");
    if (it == envelope.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number_integer()) {
        return Id{it->get<std::int64_t>()};
    }
    if (it->is_string()) {
        return Id{it->get<std::string>()};
    }
    return std::nullopt;
}

[[nodiscard]] Json build_success(const Id& id, Json result) {
    Json out = Json::object();
    out["jsonrpc"] = "2.0";
    out["id"] = id_to_json(id);
    out["result"] = std::move(result);
    return out;
}

[[nodiscard]] Json build_error(const Id& id, const ErrorObject& err) {
    Json out = Json::object();
    out["jsonrpc"] = "2.0";
    out["id"] = id_to_json(id);
    Json error_obj = Json::object();
    error_obj["code"] = err.code;
    error_obj["message"] = err.message;
    if (!err.data.is_null()) {
        error_obj["data"] = err.data;
    }
    out["error"] = std::move(error_obj);
    return out;
}

}  // namespace

void JsonRpcDispatcher::on_request(std::string method, RequestHandler handler) {
    request_handlers_[std::move(method)] = std::move(handler);
}

void JsonRpcDispatcher::on_notification(std::string method, NotificationHandler handler) {
    notification_handlers_[std::move(method)] = std::move(handler);
}

std::string JsonRpcDispatcher::error_response(const Id& id, const ErrorObject& err) {
    return build_error(id, err).dump();
}

std::string JsonRpcDispatcher::handle_request(const Request& req) const {
    const auto it = request_handlers_.find(req.method);
    if (it == request_handlers_.end()) {
        ErrorObject err;
        err.code = error_code::k_method_not_found;
        err.message = "method not found: " + req.method;
        return error_response(req.id, err);
    }

    // Handler invocation is wrapped so a thrown handler never escapes the
    // dispatcher (per ADR 0014: no exceptions across LSP boundaries).
    std::expected<Json, ErrorObject> out = std::unexpected(ErrorObject{});
    try {
        out = it->second(req.params);
    } catch (const std::exception& ex) {
        ErrorObject err;
        err.code = error_code::k_internal_error;
        err.message = std::string("handler threw: ") + ex.what();
        return error_response(req.id, err);
    } catch (...) {
        ErrorObject err;
        err.code = error_code::k_internal_error;
        err.message = "handler threw unknown exception";
        return error_response(req.id, err);
    }

    if (!out.has_value()) {
        return error_response(req.id, out.error());
    }
    return build_success(req.id, std::move(out).value()).dump();
}

void JsonRpcDispatcher::handle_notification(const Notification& note) const {
    const auto it = notification_handlers_.find(note.method);
    if (it == notification_handlers_.end()) {
        // Unknown notifications are silently ignored — that is the JSON-RPC
        // 2.0 specified behaviour and matches what every other LSP server
        // does in practice.
        return;
    }
    try {
        it->second(note.params);
    } catch (...) {
        // Notifications produce no response; any thrown exception from a
        // handler is swallowed here. The handler should have logged before
        // returning if it cared.
    }
}

std::string JsonRpcDispatcher::dispatch(const Json& envelope) const {
    if (!envelope.is_object()) {
        // No id we can echo back; reply with a synthetic null-id error so a
        // strict client at least sees something useful.
        ErrorObject err;
        err.code = error_code::k_invalid_request;
        err.message = "envelope is not a JSON object";
        return error_response(Id{std::int64_t{0}}, err);
    }

    const auto method_it = envelope.find("method");
    if (method_it == envelope.end() || !method_it->is_string()) {
        const auto id_opt = parse_id(envelope);
        if (id_opt.has_value()) {
            ErrorObject err;
            err.code = error_code::k_invalid_request;
            err.message = "missing or non-string `method`";
            return error_response(*id_opt, err);
        }
        return {};
    }

    Json params = Json::object();
    if (const auto pit = envelope.find("params"); pit != envelope.end()) {
        params = *pit;
    }

    const auto id_opt = parse_id(envelope);
    if (id_opt.has_value()) {
        Request req;
        req.id = *id_opt;
        req.method = method_it->get<std::string>();
        req.params = std::move(params);
        return handle_request(req);
    }
    Notification note;
    note.method = method_it->get<std::string>();
    note.params = std::move(params);
    handle_notification(note);
    return {};
}

std::string JsonRpcDispatcher::dispatch_wire(std::string_view wire_body) const {
    Json envelope;
    try {
        envelope = Json::parse(wire_body);
    } catch (const Json::parse_error&) {
        ErrorObject err;
        err.code = error_code::k_parse_error;
        err.message = "JSON parse error";
        return error_response(Id{std::int64_t{0}}, err);
    } catch (...) {
        ErrorObject err;
        err.code = error_code::k_parse_error;
        err.message = "unknown JSON parse failure";
        return error_response(Id{std::int64_t{0}}, err);
    }
    return dispatch(envelope);
}

}  // namespace shader_clippy::lsp::rpc
