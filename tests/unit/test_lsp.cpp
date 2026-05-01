// LSP smoke tests for sub-phase 5a (per ADR 0014).
//
// Covers:
//   1. Framing — Content-Length-prefixed message parses correctly.
//   2. DocumentManager — open / change / close round-trip.
//   3. initialize — returns ServerCapabilities with textDocumentSync /
//      diagnosticProvider.
//   4. didOpen — triggers a publishDiagnostics notification.
//   5. Diagnostic conversion — Diagnostic → LSP Diagnostic preserves range,
//      severity, code, and source.
//   6. Optional: --target-profile is plumbed via LintOptions on the lint
//      path (smoke; we just confirm the field default round-trips).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "document/manager.hpp"
#include "document/uri.hpp"
#include "hlsl_clippy/diagnostic.hpp"
#include "hlsl_clippy/lint.hpp"
#include "hlsl_clippy/source.hpp"
#include "rpc/dispatcher.hpp"
#include "rpc/framing.hpp"
#include "server/diagnostic_convert.hpp"
#include "server/handlers.hpp"

namespace lsp_doc = hlsl_clippy::lsp::document;
namespace lsp_rpc = hlsl_clippy::lsp::rpc;
namespace lsp_server = hlsl_clippy::lsp::server;

namespace {

[[nodiscard]] std::string make_framed(std::string_view body) {
    return lsp_rpc::frame_message(body);
}

}  // namespace

TEST_CASE("LSP framing parses a Content-Length-prefixed message", "[lsp][framing]") {
    const std::string body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    const std::string wire = lsp_rpc::frame_message(body);
    REQUIRE(wire.find("Content-Length: ") == 0);

    std::stringstream ss(wire);
    const auto result = lsp_rpc::read_message(ss);
    REQUIRE(result.status == lsp_rpc::FramingStatus::Ok);
    REQUIRE(result.body == body);
}

TEST_CASE("LSP framing rejects malformed headers", "[lsp][framing]") {
    // No Content-Length at all.
    {
        std::stringstream ss("Content-Type: application/json\r\n\r\n");
        const auto result = lsp_rpc::read_message(ss);
        REQUIRE(result.status == lsp_rpc::FramingStatus::HeaderError);
    }
    // EOF mid-body.
    {
        std::stringstream ss("Content-Length: 100\r\n\r\nshort");
        const auto result = lsp_rpc::read_message(ss);
        REQUIRE(result.status == lsp_rpc::FramingStatus::BodyTruncated);
    }
}

TEST_CASE("DocumentManager open / change / close roundtrip", "[lsp][document]") {
    lsp_doc::DocumentManager docs;

    constexpr const char* k_uri = "file:///tmp/foo.hlsl";
    const std::string contents = "float4 main() : SV_Target { return 1.0; }\n";
    auto& doc = docs.open(k_uri, std::filesystem::path{"/tmp/foo.hlsl"}, contents, /*version=*/1);
    REQUIRE(doc.contents == contents);
    REQUIRE(doc.version == 1);
    REQUIRE(docs.contains(k_uri));

    // Full-document replace.
    {
        std::vector<lsp_doc::ContentChange> changes(1);
        changes[0].has_range = false;
        changes[0].text = "float4 main() : SV_Target { return 0.5; }\n";
        REQUIRE(docs.apply_changes(k_uri, /*new_version=*/2, changes));
        REQUIRE(docs.find(k_uri)->contents == changes[0].text);
        REQUIRE(docs.find(k_uri)->version == 2);
    }

    // Incremental edit — replace the `0.5` with `0.25` on line 0.
    {
        std::vector<lsp_doc::ContentChange> changes(1);
        changes[0].has_range = true;
        changes[0].start_line = 0;
        // "float4 main() : SV_Target { return " has length 35 → column 35.
        changes[0].start_character = 35;
        changes[0].end_line = 0;
        changes[0].end_character = 38;  // "0.5"
        changes[0].text = "0.25";
        REQUIRE(docs.apply_changes(k_uri, /*new_version=*/3, changes));
        const auto* d = docs.find(k_uri);
        REQUIRE(d != nullptr);
        REQUIRE(d->contents.find("0.25") != std::string::npos);
        REQUIRE(d->version == 3);
    }

    docs.close(k_uri);
    REQUIRE(!docs.contains(k_uri));
    REQUIRE(docs.find(k_uri) == nullptr);
}

TEST_CASE("initialize returns ServerCapabilities with the expected shape", "[lsp][initialize]") {
    const auto caps = lsp_server::build_server_capabilities();
    REQUIRE(caps.is_object());

    REQUIRE(caps.contains("textDocumentSync"));
    const auto& sync = caps["textDocumentSync"];
    REQUIRE(sync.is_object());
    REQUIRE(sync["openClose"].get<bool>() == true);
    REQUIRE(sync["change"].get<int>() == 2);  // Incremental

    REQUIRE(caps.contains("diagnosticProvider"));
    REQUIRE(caps["diagnosticProvider"].is_object());

    // Sub-phase 5b stubs — must be advertised as off / null today.
    REQUIRE(caps["codeActionProvider"].get<bool>() == false);
    REQUIRE(caps["hoverProvider"].get<bool>() == false);
}

TEST_CASE("textDocument/didOpen triggers a publishDiagnostics notification", "[lsp][handlers]") {
    lsp_rpc::JsonRpcDispatcher dispatcher;
    lsp_doc::DocumentManager docs;

    std::vector<nlohmann::json> outbound;
    auto sink = [&outbound](const nlohmann::json& env) { outbound.push_back(env); };

    lsp_server::Server server(dispatcher, docs, sink);
    server.register_handlers();

    // Initialize the server first.
    {
        nlohmann::json init = nlohmann::json::object();
        init["jsonrpc"] = "2.0";
        init["id"] = 1;
        init["method"] = "initialize";
        init["params"] = nlohmann::json::object();
        const auto reply = dispatcher.dispatch(init);
        REQUIRE(!reply.empty());
    }

    // didOpen for an in-memory HLSL buffer that should fire `pow-const-squared`.
    nlohmann::json open_msg = nlohmann::json::object();
    open_msg["jsonrpc"] = "2.0";
    open_msg["method"] = "textDocument/didOpen";
    nlohmann::json td = nlohmann::json::object();
    td["uri"] = "file:///tmp/test.hlsl";
    td["languageId"] = "hlsl";
    td["version"] = 1;
    td["text"] = "float4 PSMain(float2 uv : TEXCOORD) : SV_Target { return pow(uv.x, 2.0); }\n";
    nlohmann::json params = nlohmann::json::object();
    params["textDocument"] = std::move(td);
    open_msg["params"] = std::move(params);

    const auto reply = dispatcher.dispatch(open_msg);
    REQUIRE(reply.empty());  // notifications never reply.

    // The handler should have published diagnostics. We expect at least one
    // outbound publishDiagnostics envelope; the diagnostics array may be
    // empty if the rule pack does not flag this exact buffer, but the
    // notification itself must have arrived.
    bool saw_publish = false;
    for (const auto& env : outbound) {
        if (env.value("method", std::string{}) == "textDocument/publishDiagnostics") {
            saw_publish = true;
            REQUIRE(env.contains("params"));
            REQUIRE(env["params"].contains("uri"));
            REQUIRE(env["params"]["uri"].get<std::string>() == "file:///tmp/test.hlsl");
            REQUIRE(env["params"].contains("diagnostics"));
            REQUIRE(env["params"]["diagnostics"].is_array());
        }
    }
    REQUIRE(saw_publish);
}

TEST_CASE("Diagnostic conversion preserves range, severity, code, source",
          "[lsp][diagnostic_convert]") {
    hlsl_clippy::SourceManager sources;
    const std::string buf = "float4 f() { return pow(x, 2.0); }\n";
    const auto src = sources.add_buffer("test.hlsl", buf);
    REQUIRE(src.valid());

    // Build a synthetic Diagnostic pointing at "pow(x, 2.0)" — bytes 20..31
    // (just an example; the exact range is not load-bearing for this test).
    hlsl_clippy::Diagnostic d;
    d.code = "pow-const-squared";
    d.severity = hlsl_clippy::Severity::Warning;
    d.message = "use x*x instead of pow(x, 2.0)";
    d.primary_span.source = src;
    d.primary_span.bytes.lo = 20U;
    d.primary_span.bytes.hi = 31U;

    hlsl_clippy::Fix fix;
    fix.description = "Replace with x*x";
    fix.machine_applicable = true;
    hlsl_clippy::TextEdit edit;
    edit.span.source = src;
    edit.span.bytes.lo = 20U;
    edit.span.bytes.hi = 31U;
    edit.replacement = "(x*x)";
    fix.edits.push_back(std::move(edit));
    d.fixes.push_back(std::move(fix));

    const auto json = lsp_server::to_lsp_diagnostic(d, sources);
    REQUIRE(json.is_object());
    REQUIRE(json["source"].get<std::string>() == "hlsl-clippy");
    REQUIRE(json["code"].get<std::string>() == "pow-const-squared");
    REQUIRE(json["severity"].get<int>() == 2);  // LSP Warning
    REQUIRE(json["message"].get<std::string>() == d.message);

    REQUIRE(json["range"].is_object());
    REQUIRE(json["range"]["start"].is_object());
    REQUIRE(json["range"]["start"]["line"].get<int>() == 0);
    REQUIRE(json["range"]["start"]["character"].get<int>() == 20);
    REQUIRE(json["range"]["end"]["character"].get<int>() == 31);
}

TEST_CASE("LintOptions::target_profile defaults round-trip", "[lsp][lint_options]") {
    // Smoke test that LintOptions{} constructs with a null/empty
    // target_profile (the LSP path passes this through unchanged today;
    // sub-phase 5b will add a settings-driven override).
    hlsl_clippy::LintOptions options;
    REQUIRE(!options.target_profile.has_value());
    REQUIRE(options.enable_reflection);
    REQUIRE(options.reflection_pool_size == 4U);
}

TEST_CASE("URI roundtrip preserves the path component", "[lsp][uri]") {
#if defined(_WIN32)
    const std::string uri = "file:///C:/tmp/foo.hlsl";
    const auto path = lsp_doc::uri_to_path(uri);
    REQUIRE(path.has_value());
    const auto roundtrip = lsp_doc::path_to_uri(*path);
    REQUIRE(roundtrip == uri);
#else
    const std::string uri = "file:///tmp/foo.hlsl";
    const auto path = lsp_doc::uri_to_path(uri);
    REQUIRE(path.has_value());
    const auto roundtrip = lsp_doc::path_to_uri(*path);
    REQUIRE(roundtrip == uri);
#endif
}

TEST_CASE("Dispatcher routes requests and notifications", "[lsp][dispatcher]") {
    lsp_rpc::JsonRpcDispatcher d;
    int notify_count = 0;
    d.on_request(
        "ping",
        [](const nlohmann::json& /*p*/) -> std::expected<nlohmann::json, lsp_rpc::ErrorObject> {
            return nlohmann::json{"pong"};
        });
    d.on_notification("buzz", [&notify_count](const nlohmann::json& /*p*/) { ++notify_count; });

    nlohmann::json req = nlohmann::json::object();
    req["jsonrpc"] = "2.0";
    req["id"] = 42;
    req["method"] = "ping";
    req["params"] = nlohmann::json::object();
    const auto reply = d.dispatch(req);
    REQUIRE(!reply.empty());
    const auto reply_json = nlohmann::json::parse(reply);
    REQUIRE(reply_json["id"].get<int>() == 42);
    REQUIRE(reply_json["result"].get<std::string>() == "pong");

    // Method not found surfaces as an error response with the same id.
    nlohmann::json bad = nlohmann::json::object();
    bad["jsonrpc"] = "2.0";
    bad["id"] = "x";
    bad["method"] = "does-not-exist";
    const auto err = d.dispatch(bad);
    const auto err_json = nlohmann::json::parse(err);
    REQUIRE(err_json["error"]["code"].get<int>() == lsp_rpc::error_code::k_method_not_found);

    // Notification — no reply, side effect captured.
    nlohmann::json note = nlohmann::json::object();
    note["jsonrpc"] = "2.0";
    note["method"] = "buzz";
    REQUIRE(d.dispatch(note).empty());
    REQUIRE(notify_count == 1);
}
