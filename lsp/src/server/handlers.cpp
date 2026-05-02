// LSP handler implementations.

#include "server/handlers.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "document/manager.hpp"
#include "document/uri.hpp"
#include "hlsl_clippy/config.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/rule.hpp"
#include "hlsl_clippy/source.hpp"
#include "hlsl_clippy/version.hpp"
#include "rpc/dispatcher.hpp"
#include "rpc/message.hpp"
#include "server/code_actions.hpp"
#include "server/diagnostic_convert.hpp"

#include "config_resolver.hpp"

namespace hlsl_clippy::lsp::server {

namespace {

/// LSP TextDocumentSyncKind values (only `Incremental` is used today;
/// `None` and `Full` are documented in §"TextDocumentSyncKind" of the LSP
/// spec but we never advertise either kind in our capabilities).
constexpr int k_sync_incremental = 2;

/// Pull the URI out of a `textDocument` sub-object. Returns empty string on
/// missing/malformed input.
[[nodiscard]] std::string extract_text_document_uri(const nlohmann::json& params) {
    if (!params.is_object()) {
        return {};
    }
    const auto td_it = params.find("textDocument");
    if (td_it == params.end() || !td_it->is_object()) {
        return {};
    }
    const auto uri_it = td_it->find("uri");
    if (uri_it == td_it->end() || !uri_it->is_string()) {
        return {};
    }
    return uri_it->get<std::string>();
}

[[nodiscard]] std::int32_t extract_text_document_version(const nlohmann::json& params) {
    if (!params.is_object()) {
        return 0;
    }
    const auto td_it = params.find("textDocument");
    if (td_it == params.end() || !td_it->is_object()) {
        return 0;
    }
    const auto v_it = td_it->find("version");
    if (v_it == td_it->end() || !v_it->is_number_integer()) {
        return 0;
    }
    return v_it->get<std::int32_t>();
}

[[nodiscard]] std::vector<document::ContentChange> parse_content_changes(
    const nlohmann::json& params) {
    std::vector<document::ContentChange> out;
    if (!params.is_object()) {
        return out;
    }
    const auto changes_it = params.find("contentChanges");
    if (changes_it == params.end() || !changes_it->is_array()) {
        return out;
    }
    for (const auto& change : *changes_it) {
        document::ContentChange c;
        const auto text_it = change.find("text");
        if (text_it != change.end() && text_it->is_string()) {
            c.text = text_it->get<std::string>();
        }
        const auto range_it = change.find("range");
        if (range_it != change.end() && range_it->is_object()) {
            const auto start_it = range_it->find("start");
            const auto end_it = range_it->find("end");
            if (start_it != range_it->end() && end_it != range_it->end() && start_it->is_object() &&
                end_it->is_object()) {
                const auto sl = start_it->find("line");
                const auto sc = start_it->find("character");
                const auto el = end_it->find("line");
                const auto ec = end_it->find("character");
                if (sl != start_it->end() && sc != start_it->end() && el != end_it->end() &&
                    ec != end_it->end() && sl->is_number_integer() && sc->is_number_integer() &&
                    el->is_number_integer() && ec->is_number_integer()) {
                    c.has_range = true;
                    c.start_line = sl->get<std::uint32_t>();
                    c.start_character = sc->get<std::uint32_t>();
                    c.end_line = el->get<std::uint32_t>();
                    c.end_character = ec->get<std::uint32_t>();
                }
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace

nlohmann::json build_server_capabilities() {
    nlohmann::json caps = nlohmann::json::object();

    // textDocumentSync — full object form per LSP §"TextDocumentSyncOptions"
    // so we can declare openClose + save + change kind in one block.
    nlohmann::json sync = nlohmann::json::object();
    sync["openClose"] = true;
    sync["change"] = k_sync_incremental;
    nlohmann::json save = nlohmann::json::object();
    save["includeText"] = false;
    sync["save"] = std::move(save);
    caps["textDocumentSync"] = std::move(sync);

    // Capabilities advertised by sub-phase 5b. `codeActionProvider` is the
    // boolean form (we accept any kind/range and return only what we have);
    // `hoverProvider` is unconditional — we only return content when the
    // cursor sits on a diagnostic span.
    caps["codeActionProvider"] = true;        // sub-phase 5b
    caps["hoverProvider"] = true;             // sub-phase 5b
    caps["completionProvider"] = nullptr;     // out of scope per ADR 0014 §5
    caps["signatureHelpProvider"] = nullptr;  // out of scope
    caps["definitionProvider"] = false;       // out of scope
    caps["referencesProvider"] = false;       // out of scope
    caps["documentFormattingProvider"] = false;
    caps["documentRangeFormattingProvider"] = false;

    // We push diagnostics via the `textDocument/publishDiagnostics`
    // notification (implicit; no capability to advertise). Do NOT advertise
    // `diagnosticProvider` — that is the LSP 3.17 *pull* model and would
    // make clients send `textDocument/diagnostic` requests we don't handle.

    // workspace block — multi-root support (lazy: we resolve config per-doc
    // anyway).
    nlohmann::json workspace = nlohmann::json::object();
    nlohmann::json workspace_folders = nlohmann::json::object();
    workspace_folders["supported"] = true;
    workspace_folders["changeNotifications"] = true;
    workspace["workspaceFolders"] = std::move(workspace_folders);
    caps["workspace"] = std::move(workspace);

    // positionEncoding — we currently expect UTF-8 byte offsets (see
    // DocumentManager::position_to_offset note). Declaring "utf-8" requires
    // the client to support it; to maximise compatibility, leave the field
    // unset (LSP spec default = utf-16). Pure-ASCII shaders are identical.
    return caps;
}

Server::Server(rpc::JsonRpcDispatcher& dispatcher,
               document::DocumentManager& docs,
               NotificationSink sink)
    : dispatcher_(&dispatcher), docs_(&docs), sink_(std::move(sink)) {}

void Server::register_handlers() {
    dispatcher_->on_request("initialize",
                            [this](const nlohmann::json& p) { return on_initialize(p); });
    dispatcher_->on_request("shutdown", [this](const nlohmann::json& p) { return on_shutdown(p); });

    // Sub-phase 5b request handlers.
    dispatcher_->on_request("textDocument/codeAction",
                            [this](const nlohmann::json& p) { return on_code_action(p); });
    dispatcher_->on_request("textDocument/hover",
                            [this](const nlohmann::json& p) { return on_hover(p); });

    dispatcher_->on_notification("initialized",
                                 [this](const nlohmann::json& p) { on_initialized(p); });
    dispatcher_->on_notification("textDocument/didOpen",
                                 [this](const nlohmann::json& p) { on_did_open(p); });
    dispatcher_->on_notification("textDocument/didChange",
                                 [this](const nlohmann::json& p) { on_did_change(p); });
    dispatcher_->on_notification("textDocument/didSave",
                                 [this](const nlohmann::json& p) { on_did_save(p); });
    dispatcher_->on_notification("textDocument/didClose",
                                 [this](const nlohmann::json& p) { on_did_close(p); });
    dispatcher_->on_notification(
        "workspace/didChangeConfiguration",
        [this](const nlohmann::json& p) { on_did_change_configuration(p); });
    dispatcher_->on_notification(
        "workspace/didChangeWatchedFiles",
        [this](const nlohmann::json& p) { on_did_change_watched_files(p); });
    dispatcher_->on_notification("exit", [this](const nlohmann::json& p) { on_exit(p); });
}

std::expected<nlohmann::json, rpc::ErrorObject> Server::on_initialize(
    const nlohmann::json& /*params*/) {
    nlohmann::json result = nlohmann::json::object();
    result["capabilities"] = build_server_capabilities();
    nlohmann::json server_info = nlohmann::json::object();
    server_info["name"] = "hlsl-clippy-lsp";
    server_info["version"] = std::string{hlsl_clippy::version()};
    result["serverInfo"] = std::move(server_info);
    return result;
}

std::expected<nlohmann::json, rpc::ErrorObject> Server::on_shutdown(
    const nlohmann::json& /*params*/) {
    shutdown_requested_ = true;
    return nlohmann::json{};  // null result per LSP spec.
}

std::expected<nlohmann::json, rpc::ErrorObject> Server::on_unimplemented_request(
    const nlohmann::json& /*params*/) {
    // Capability advertised as off — return null (success-with-no-result)
    // so a misbehaving client that asks anyway gets a clean reply rather
    // than a method-not-found error.
    return nlohmann::json{};
}

namespace {

/// Read a `{"line": int, "character": int}` LSP Position from `obj`. Returns
/// `(0, 0)` when the field is missing or malformed — that is the safest
/// fallback because LSP positions are always non-negative.
[[nodiscard]] std::pair<std::int32_t, std::int32_t> extract_position(const nlohmann::json& obj) {
    std::int32_t line = 0;
    std::int32_t character = 0;
    if (obj.is_object()) {
        const auto l = obj.find("line");
        const auto c = obj.find("character");
        if (l != obj.end() && l->is_number_integer()) {
            line = l->get<std::int32_t>();
        }
        if (c != obj.end() && c->is_number_integer()) {
            character = c->get<std::int32_t>();
        }
    }
    return {line, character};
}

/// View into an open document for hover / code-action handlers. Holds a
/// freshly-constructed `SourceManager` so byte→line/col lookups against the
/// stored `latest_diagnostics` resolve correctly, but reuses the cached
/// diagnostics from the most recent `lint_and_publish` instead of re-running
/// the lint pipeline.
///
/// Pre-refactor, every textDocument/hover and textDocument/codeAction
/// request triggered a fresh `lint()` call -- parser + AST walk + reflection
/// + CFG, on the order of tens of ms per source on warm caches and hundreds
/// of ms cold. Hover requests in particular are sent at IDE typing cadence
/// (often once per cursor move) and re-linting per request was a measurable
/// idle-CPU drain.
///
/// SourceManager construction is cheap (one unique_ptr allocation + a copy
/// of `doc->contents`); the diagnostics already live on the OpenDocument.
struct DocumentView {
    hlsl_clippy::SourceManager sources;
    const std::vector<hlsl_clippy::Diagnostic>* diagnostics = nullptr;
    hlsl_clippy::SourceId src_id;
};

[[nodiscard]] std::optional<DocumentView> view_for(document::DocumentManager& docs,
                                                   const std::string& uri) {
    auto* doc = docs.find(uri);
    if (doc == nullptr) {
        return std::nullopt;
    }
    DocumentView view;
    view.src_id = view.sources.add_buffer(doc->path.string(), doc->contents);
    if (!view.src_id.valid()) {
        return std::nullopt;
    }
    view.diagnostics = &doc->latest_diagnostics;
    return view;
}

}  // namespace

std::expected<nlohmann::json, rpc::ErrorObject> Server::on_code_action(
    const nlohmann::json& params) {
    const auto uri = extract_text_document_uri(params);
    if (uri.empty()) {
        return nlohmann::json::array();
    }
    if (!params.is_object()) {
        return nlohmann::json::array();
    }
    const auto range_it = params.find("range");
    if (range_it == params.end() || !range_it->is_object()) {
        return nlohmann::json::array();
    }
    const auto start_it = range_it->find("start");
    const auto end_it = range_it->find("end");
    if (start_it == range_it->end() || end_it == range_it->end()) {
        return nlohmann::json::array();
    }
    const auto [s_line, s_char] = extract_position(*start_it);
    const auto [e_line, e_char] = extract_position(*end_it);

    auto view = view_for(*docs_, uri);
    if (!view.has_value() || view->diagnostics == nullptr) {
        return nlohmann::json::array();
    }
    return code_actions_for_range(
        *view->diagnostics, view->sources, uri, s_line, s_char, e_line, e_char);
}

std::expected<nlohmann::json, rpc::ErrorObject> Server::on_hover(const nlohmann::json& params) {
    const auto uri = extract_text_document_uri(params);
    if (uri.empty()) {
        return nlohmann::json{};
    }
    if (!params.is_object()) {
        return nlohmann::json{};
    }
    const auto pos_it = params.find("position");
    if (pos_it == params.end() || !pos_it->is_object()) {
        return nlohmann::json{};
    }
    const auto [line, character] = extract_position(*pos_it);

    auto view = view_for(*docs_, uri);
    if (!view.has_value() || view->diagnostics == nullptr) {
        return nlohmann::json{};
    }
    return hover_for_position(*view->diagnostics, view->sources, line, character);
}

void Server::on_initialized(const nlohmann::json& /*params*/) {
    initialized_ = true;
}

void Server::on_did_open(const nlohmann::json& params) {
    const auto uri = extract_text_document_uri(params);
    if (uri.empty()) {
        return;
    }
    if (!params.is_object()) {
        return;
    }
    const auto td_it = params.find("textDocument");
    if (td_it == params.end() || !td_it->is_object()) {
        return;
    }
    const auto text_it = td_it->find("text");
    if (text_it == td_it->end() || !text_it->is_string()) {
        return;
    }
    const auto path_opt = document::uri_to_path(uri);
    const std::filesystem::path path = path_opt.value_or(std::filesystem::path{uri});
    docs_->open(uri, path, text_it->get<std::string>(), extract_text_document_version(params));
    lint_and_publish(uri);
}

void Server::on_did_change(const nlohmann::json& params) {
    const auto uri = extract_text_document_uri(params);
    if (uri.empty()) {
        return;
    }
    const auto changes = parse_content_changes(params);
    if (!docs_->apply_changes(uri, extract_text_document_version(params), changes)) {
        return;
    }
    // Debounce: skip the lint when the document was changed within the last
    // 150 ms and we already pushed diagnostics recently. A future sub-phase
    // can replace this with a real timer; the in-line gate is sufficient
    // for correctness today.
    if (docs_->should_debounce_lint(uri)) {
        return;
    }
    lint_and_publish(uri);
}

void Server::on_did_save(const nlohmann::json& params) {
    const auto uri = extract_text_document_uri(params);
    if (uri.empty()) {
        return;
    }
    lint_and_publish(uri);
}

void Server::on_did_close(const nlohmann::json& params) {
    const auto uri = extract_text_document_uri(params);
    if (uri.empty()) {
        return;
    }
    docs_->close(uri);
    // Per LSP §"publishDiagnostics", clients keep the last-published
    // diagnostics until the server clears them — push an empty list.
    nlohmann::json clear_params = nlohmann::json::object();
    clear_params["uri"] = uri;
    clear_params["diagnostics"] = nlohmann::json::array();
    send_notification("textDocument/publishDiagnostics", std::move(clear_params));
}

void Server::on_did_change_configuration(const nlohmann::json& /*params*/) {
    // Re-lint-on-config-change is a sub-phase 5b enhancement. For 5a we
    // accept the notification (so the dispatcher does not log an
    // unknown-method warning) but do not act on it; the next didChange /
    // didSave on each open document picks up the new config naturally
    // because resolve_config_for() re-reads .hlsl-clippy.toml every call.
}

void Server::on_did_change_watched_files(const nlohmann::json& /*params*/) {
    // Same shape as on_did_change_configuration — accept the notification
    // but defer the re-lint loop to sub-phase 5b. Until then, edits to
    // .hlsl-clippy.toml are picked up on the next didChange / didSave per
    // affected document.
}

void Server::on_exit(const nlohmann::json& /*params*/) {
    should_exit_ = true;
}

void Server::lint_and_publish(const std::string& uri) {
    auto* doc = docs_->find(uri);
    if (doc == nullptr) {
        return;
    }

    hlsl_clippy::SourceManager sources;
    const auto src_id = sources.add_buffer(doc->path.string(), doc->contents);
    if (!src_id.valid()) {
        return;
    }

    auto rules = hlsl_clippy::make_default_rules();

    std::vector<hlsl_clippy::Diagnostic> diagnostics;
    auto config = lsp::resolve_config_for(doc->path);
    hlsl_clippy::LintOptions options;
    if (config.has_value()) {
        diagnostics = hlsl_clippy::lint(sources, src_id, rules, *config, doc->path, options);
    } else {
        diagnostics = hlsl_clippy::lint(sources, src_id, rules, options);
    }

    doc->latest_diagnostics = diagnostics;
    docs_->mark_linted(uri);

    nlohmann::json p = nlohmann::json::object();
    p["uri"] = uri;
    p["version"] = doc->version;
    p["diagnostics"] = to_lsp_diagnostics(diagnostics, sources);
    send_notification("textDocument/publishDiagnostics", std::move(p));
}

void Server::send_notification(const std::string& method, nlohmann::json params) {
    if (!sink_) {
        return;
    }
    nlohmann::json envelope = nlohmann::json::object();
    envelope["jsonrpc"] = "2.0";
    envelope["method"] = method;
    envelope["params"] = std::move(params);
    sink_(envelope);
}

}  // namespace hlsl_clippy::lsp::server
