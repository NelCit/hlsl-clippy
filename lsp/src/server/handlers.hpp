// LSP method handlers — initialize, didOpen, didChange, didSave, didClose,
// publishDiagnostics, shutdown, exit.
//
// Per ADR 0014 §"LSP capabilities (initial scope)", sub-phase 5a wires:
//   - initialize / initialized
//   - shutdown / exit
//   - textDocument/didOpen
//   - textDocument/didChange (incremental sync)
//   - textDocument/didSave
//   - textDocument/didClose
//   - publishDiagnostics (server-initiated notification)
//   - workspace/configuration (no-op stub for 5a)
//
// codeAction, hover, signatureHelp, definition, references, completion,
// formatting are advertised as `false` / empty — sub-phase 5b layers
// codeAction + hover on top.

#pragma once

#include <expected>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "document/manager.hpp"
#include "rpc/dispatcher.hpp"
#include "rpc/message.hpp"

namespace shader_clippy::lsp::server {

/// Sink for outbound notifications from the server. The framing layer wraps
/// the body before writing it to stdout.
using NotificationSink = std::function<void(const nlohmann::json& envelope)>;

/// Aggregates the open-document registry and the dispatcher hook-up for
/// the LSP server. Owns no I/O — `main.cpp` injects the notification sink.
class Server {
public:
    Server(rpc::JsonRpcDispatcher& dispatcher,
           document::DocumentManager& docs,
           NotificationSink sink);

    /// Wire every handler registered for sub-phase 5a onto `dispatcher`.
    /// Idempotent — a second call replaces the bindings.
    void register_handlers();

    /// True when the client sent `shutdown` and we should stop after the
    /// next `exit` notification.
    [[nodiscard]] bool shutdown_requested() const noexcept {
        return shutdown_requested_;
    }

    /// True when the client sent `exit` and the loop should terminate.
    [[nodiscard]] bool should_exit() const noexcept {
        return should_exit_;
    }

    /// Exit code the process should return. Per LSP spec: 0 if `exit`
    /// arrived after `shutdown`, 1 otherwise.
    [[nodiscard]] int exit_code() const noexcept {
        return shutdown_requested_ ? 0 : 1;
    }

private:
    // ── request handlers ──────────────────────────────────────────────────
    [[nodiscard]] std::expected<nlohmann::json, rpc::ErrorObject> on_initialize(
        const nlohmann::json& params);
    [[nodiscard]] std::expected<nlohmann::json, rpc::ErrorObject> on_shutdown(
        const nlohmann::json& params);
    [[nodiscard]] std::expected<nlohmann::json, rpc::ErrorObject> on_unimplemented_request(
        const nlohmann::json& params);
    [[nodiscard]] std::expected<nlohmann::json, rpc::ErrorObject> on_code_action(
        const nlohmann::json& params);
    [[nodiscard]] std::expected<nlohmann::json, rpc::ErrorObject> on_hover(
        const nlohmann::json& params);

    // ── notification handlers ─────────────────────────────────────────────
    void on_initialized(const nlohmann::json& params);
    void on_did_open(const nlohmann::json& params);
    void on_did_change(const nlohmann::json& params);
    void on_did_save(const nlohmann::json& params);
    void on_did_close(const nlohmann::json& params);
    void on_did_change_configuration(const nlohmann::json& params);
    void on_did_change_watched_files(const nlohmann::json& params);
    void on_exit(const nlohmann::json& params);

    /// Run the lint pipeline against `uri` and emit a `publishDiagnostics`
    /// notification with the result. Safe to call when the document is
    /// not registered — does nothing in that case.
    void lint_and_publish(const std::string& uri);

    /// Helper: send a notification through the configured sink.
    void send_notification(const std::string& method, nlohmann::json params);

    rpc::JsonRpcDispatcher* dispatcher_;
    document::DocumentManager* docs_;
    NotificationSink sink_;

    bool initialized_ = false;
    bool shutdown_requested_ = false;
    bool should_exit_ = false;
};

/// Build the JSON `ServerCapabilities` object advertised in `initialize`'s
/// response. Public for unit tests.
[[nodiscard]] nlohmann::json build_server_capabilities();

}  // namespace shader_clippy::lsp::server
