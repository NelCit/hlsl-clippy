// LSP smoke tests for sub-phase 5a (per ADR 0014).
//
// Covers:
//   1. Framing — Content-Length-prefixed message parses correctly.
//   2. DocumentManager — open / change / close round-trip.
//   3. initialize — returns ServerCapabilities with textDocumentSync and
//      no diagnosticProvider (we use push diagnostics, not pull).
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
#include "server/code_actions.hpp"
#include "server/diagnostic_convert.hpp"
#include "server/handlers.hpp"

namespace lsp_doc = hlsl_clippy::lsp::document;
namespace lsp_rpc = hlsl_clippy::lsp::rpc;
namespace lsp_server = hlsl_clippy::lsp::server;

namespace {

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

    // We use push diagnostics (publishDiagnostics notification). Advertising
    // `diagnosticProvider` would put LSP 3.17 clients into pull mode and
    // they'd send `textDocument/diagnostic` requests we don't handle.
    REQUIRE(!caps.contains("diagnosticProvider"));

    // Sub-phase 5b — codeAction + hover are now advertised as on.
    REQUIRE(caps["codeActionProvider"].get<bool>() == true);
    REQUIRE(caps["hoverProvider"].get<bool>() == true);
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
            // Use the parenthesised constructor so the JSON value is the
            // string "pong" rather than the single-element array `["pong"]`
            // that the brace-init form would produce.
            return nlohmann::json(std::string{"pong"});
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

// ─── Sub-phase 5b code-action / hover tests (appended) ───────────────────────

namespace {

/// Build a synthetic Diagnostic + Fix at byte range [lo, hi) with the
/// given replacement. Helper for the 5b tests below.
[[nodiscard]] hlsl_clippy::Diagnostic make_diag_with_fix(
    hlsl_clippy::SourceId src,
    std::uint32_t lo,
    std::uint32_t hi,
    const std::string& replacement,
    bool machine_applicable,
    const std::string& code = "test-rule",
    const std::string& description = "Replace span") {
    hlsl_clippy::Diagnostic d;
    d.code = code;
    d.severity = hlsl_clippy::Severity::Warning;
    d.message = "synthetic diagnostic";
    d.primary_span.source = src;
    d.primary_span.bytes.lo = lo;
    d.primary_span.bytes.hi = hi;
    hlsl_clippy::Fix fix;
    fix.description = description;
    fix.machine_applicable = machine_applicable;
    hlsl_clippy::TextEdit edit;
    edit.span.source = src;
    edit.span.bytes.lo = lo;
    edit.span.bytes.hi = hi;
    edit.replacement = replacement;
    fix.edits.push_back(std::move(edit));
    d.fixes.push_back(std::move(fix));
    return d;
}

}  // namespace

TEST_CASE("code-action: machine-applicable Fix produces quickfix with isPreferred=true",
          "[lsp][code_action]") {
    hlsl_clippy::SourceManager sources;
    const std::string buf = "float4 f(float x) { return pow(x, 2.0); }\n";
    const auto src = sources.add_buffer("test.hlsl", buf);
    REQUIRE(src.valid());

    // "pow(x, 2.0)" starts at byte 27, length 11 → ends at byte 38.
    const auto lo = static_cast<std::uint32_t>(buf.find("pow"));
    const auto hi = lo + 11U;
    const auto d = make_diag_with_fix(
        src, lo, hi, "(x*x)", /*machine_applicable=*/true, "pow-const-squared", "Replace with x*x");
    std::vector<hlsl_clippy::Diagnostic> diags{d};

    constexpr const char* k_uri = "file:///tmp/test.hlsl";
    const auto actions = lsp_server::code_actions_for_range(diags,
                                                            sources,
                                                            k_uri,
                                                            /*line_start=*/0,
                                                            /*char_start=*/0,
                                                            /*line_end=*/0,
                                                            /*char_end=*/100);
    REQUIRE(actions.is_array());
    REQUIRE(actions.size() == 1);
    const auto& a = actions[0];
    REQUIRE(a["kind"].get<std::string>() == "quickfix");
    REQUIRE(a["title"].get<std::string>() == "Replace with x*x");
    REQUIRE(a["isPreferred"].get<bool>() == true);

    // diagnostics array carries the LSP-shaped diagnostic.
    REQUIRE(a["diagnostics"].is_array());
    REQUIRE(a["diagnostics"].size() == 1);
    REQUIRE(a["diagnostics"][0]["code"].get<std::string>() == "pow-const-squared");

    // edit.changes[uri] is a TextEdit array matching the Fix's edits
    // byte-for-byte.
    REQUIRE(a["edit"].is_object());
    REQUIRE(a["edit"]["changes"].is_object());
    REQUIRE(a["edit"]["changes"].contains(k_uri));
    const auto& text_edits = a["edit"]["changes"][k_uri];
    REQUIRE(text_edits.is_array());
    REQUIRE(text_edits.size() == 1);
    REQUIRE(text_edits[0]["newText"].get<std::string>() == "(x*x)");
    REQUIRE(text_edits[0]["range"]["start"]["line"].get<int>() == 0);
    REQUIRE(text_edits[0]["range"]["start"]["character"].get<int>() == static_cast<int>(lo));
    REQUIRE(text_edits[0]["range"]["end"]["character"].get<int>() == static_cast<int>(hi));
}

TEST_CASE("code-action: suggestion-only Fix produces quickfix with isPreferred=false",
          "[lsp][code_action]") {
    hlsl_clippy::SourceManager sources;
    const std::string buf = "float4 f(float x) { return pow(x, 2.0); }\n";
    const auto src = sources.add_buffer("test.hlsl", buf);
    REQUIRE(src.valid());

    const auto lo = static_cast<std::uint32_t>(buf.find("pow"));
    const auto hi = lo + 11U;
    const auto d = make_diag_with_fix(src, lo, hi, "(x*x)", /*machine_applicable=*/false);
    std::vector<hlsl_clippy::Diagnostic> diags{d};

    const auto actions =
        lsp_server::code_actions_for_range(diags, sources, "file:///tmp/x.hlsl", 0, 0, 0, 200);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0]["kind"].get<std::string>() == "quickfix");
    REQUIRE(actions[0]["isPreferred"].get<bool>() == false);
}

TEST_CASE("code-action: diagnostic outside the requested range is excluded", "[lsp][code_action]") {
    hlsl_clippy::SourceManager sources;
    // Two-line buffer; diagnostic on line 1, request range on line 0.
    const std::string buf = std::string{"float a = 1.0;\n"} + "float b = pow(c, 2.0);\n";
    const auto src = sources.add_buffer("test.hlsl", buf);
    REQUIRE(src.valid());

    // "pow(c, 2.0)" lives at byte offset (15 + "float b = ".len) on line 1.
    const auto pow_off = static_cast<std::uint32_t>(buf.find("pow"));
    const auto d = make_diag_with_fix(src, pow_off, pow_off + 11U, "(c*c)", true);
    std::vector<hlsl_clippy::Diagnostic> diags{d};

    // Request only line 0 (the `float a = 1.0;` line). The diagnostic on
    // line 1 must be excluded.
    const auto actions =
        lsp_server::code_actions_for_range(diags, sources, "file:///tmp/x.hlsl", 0, 0, 0, 14);
    REQUIRE(actions.is_array());
    REQUIRE(actions.empty());

    // Sanity: requesting a range that covers line 1 produces the action.
    const auto actions_hit =
        lsp_server::code_actions_for_range(diags, sources, "file:///tmp/x.hlsl", 1, 0, 1, 200);
    REQUIRE(actions_hit.size() == 1);
}

TEST_CASE("hover: diagnostic at cursor returns markdown with rule-id link", "[lsp][hover]") {
    hlsl_clippy::SourceManager sources;
    const std::string buf = "float4 f(float x) { return pow(x, 2.0); }\n";
    const auto src = sources.add_buffer("test.hlsl", buf);
    REQUIRE(src.valid());

    const auto lo = static_cast<std::uint32_t>(buf.find("pow"));
    const auto hi = lo + 11U;
    auto d =
        make_diag_with_fix(src, lo, hi, "(x*x)", true, "pow-const-squared", "Replace with x*x");
    d.message = "use x*x instead of pow(x, 2.0)";
    std::vector<hlsl_clippy::Diagnostic> diags{d};

    // Cursor inside the `pow` token (line 0, character lo+1).
    const auto hover =
        lsp_server::hover_for_position(diags, sources, 0, static_cast<std::int32_t>(lo + 1));
    REQUIRE(hover.is_object());
    REQUIRE(hover["contents"]["kind"].get<std::string>() == "markdown");
    const auto value = hover["contents"]["value"].get<std::string>();
    REQUIRE(value.find("pow-const-squared") != std::string::npos);
    REQUIRE(value.find("use x*x instead of pow(x, 2.0)") != std::string::npos);
    REQUIRE(value.find("https://github.com/NelCit/hlsl-clippy/blob/main/docs/rules/"
                       "pow-const-squared.md") != std::string::npos);

    // Cursor outside any diagnostic span returns null (no hover content).
    const auto miss = lsp_server::hover_for_position(diags, sources, 0, 0);
    REQUIRE(miss.is_null());
}

TEST_CASE("docs_url_for_rule produces the canonical pre-Phase-6 URL", "[lsp][hover]") {
    REQUIRE(lsp_server::docs_url_for_rule("pow-const-squared") ==
            "https://github.com/NelCit/hlsl-clippy/blob/main/docs/rules/pow-const-squared.md");
}

// ─── End-to-end fix-all path (regression guard) ──────────────────────────────
//
// These tests exist because both regressions we hit shortly before v0.6.8 lived
// at the LSP↔editor boundary and slipped past the unit tests above:
//
//   1. The server briefly advertised `diagnosticProvider` it didn't implement,
//      causing vscode-languageclient to spam `Document pull failed` errors.
//   2. The VS Code extension's "Fix All in Document" gathered fixes via a
//      per-diagnostic loop that round-tripped to the LSP N times and could
//      drop edits or accumulate duplicates.
//
// The first is now guarded by the capabilities test above; the cases below
// guard the second by exercising the same shape of request the extension
// actually sends after the v0.6.8 refactor: one full-document
// textDocument/codeAction request, kind=quickfix, expecting one CodeAction
// per fix with a populated `edit.changes[uri]`.

namespace {

/// Build a fresh server wired into a captured-notifications sink. Returns
/// the trio so each test can drive its own request stream without sharing
/// state with siblings.
struct ServerHarness {
    lsp_rpc::JsonRpcDispatcher dispatcher;
    lsp_doc::DocumentManager docs;
    std::vector<nlohmann::json> outbound;
    std::unique_ptr<lsp_server::Server> server;

    ServerHarness() {
        auto sink = [this](const nlohmann::json& env) { outbound.push_back(env); };
        server = std::make_unique<lsp_server::Server>(dispatcher, docs, sink);
        server->register_handlers();

        nlohmann::json init = nlohmann::json::object();
        init["jsonrpc"] = "2.0";
        init["id"] = 1;
        init["method"] = "initialize";
        init["params"] = nlohmann::json::object();
        (void)dispatcher.dispatch(init);
    }

    void did_open(const std::string& uri, const std::string& text, std::int32_t version = 1) {
        nlohmann::json open_msg = nlohmann::json::object();
        open_msg["jsonrpc"] = "2.0";
        open_msg["method"] = "textDocument/didOpen";
        nlohmann::json td = nlohmann::json::object();
        td["uri"] = uri;
        td["languageId"] = "hlsl";
        td["version"] = version;
        td["text"] = text;
        nlohmann::json params = nlohmann::json::object();
        params["textDocument"] = std::move(td);
        open_msg["params"] = std::move(params);
        (void)dispatcher.dispatch(open_msg);
    }

    [[nodiscard]] nlohmann::json published_diagnostics_for(const std::string& uri) const {
        for (const auto& env : outbound) {
            if (env.value("method", std::string{}) != "textDocument/publishDiagnostics") {
                continue;
            }
            if (env["params"]["uri"].get<std::string>() == uri) {
                return env["params"]["diagnostics"];
            }
        }
        return nlohmann::json::array();
    }

    [[nodiscard]] nlohmann::json code_action_full_document(const std::string& uri,
                                                           std::int32_t end_line,
                                                           std::int32_t end_char,
                                                           int request_id) {
        nlohmann::json req = nlohmann::json::object();
        req["jsonrpc"] = "2.0";
        req["id"] = request_id;
        req["method"] = "textDocument/codeAction";
        nlohmann::json p = nlohmann::json::object();
        nlohmann::json td = nlohmann::json::object();
        td["uri"] = uri;
        p["textDocument"] = std::move(td);
        nlohmann::json range = nlohmann::json::object();
        nlohmann::json start = nlohmann::json::object();
        start["line"] = 0;
        start["character"] = 0;
        range["start"] = std::move(start);
        nlohmann::json end = nlohmann::json::object();
        end["line"] = end_line;
        end["character"] = end_char;
        range["end"] = std::move(end);
        p["range"] = std::move(range);
        nlohmann::json ctx = nlohmann::json::object();
        ctx["diagnostics"] = nlohmann::json::array();
        ctx["only"] = nlohmann::json::array({"quickfix"});
        p["context"] = std::move(ctx);
        req["params"] = std::move(p);

        const auto wire = dispatcher.dispatch(req);
        REQUIRE(!wire.empty());
        return nlohmann::json::parse(wire);
    }
};

}  // namespace

TEST_CASE(
    "didOpen on `pow(x, 2.0)` publishes a non-empty diagnostics list "
    "(regression: must not be silently empty)",
    "[lsp][handlers][regression]") {
    ServerHarness h;
    constexpr const char* k_uri = "file:///tmp/pow_x.hlsl";
    // Bare-identifier base — pow-to-mul fires with machine_applicable=true and
    // attaches a TextEdit fix. pow-const-squared also fires but does not
    // attach a fix (Phase 0 design; fixes deferred). At least one of the two
    // must be in the published list, otherwise the LSP plumbing is broken.
    h.did_open(k_uri, "float pow_squared(float x) { return pow(x, 2.0); }\n");

    const auto diags = h.published_diagnostics_for(k_uri);
    REQUIRE(diags.is_array());
    REQUIRE(!diags.empty());

    bool saw_pow_to_mul = false;
    for (const auto& d : diags) {
        if (d.value("code", std::string{}) == "pow-to-mul") {
            saw_pow_to_mul = true;
            break;
        }
    }
    REQUIRE(saw_pow_to_mul);
}

TEST_CASE(
    "textDocument/codeAction over the full document returns a quickfix "
    "with edit.changes[uri] populated",
    "[lsp][handlers][regression]") {
    ServerHarness h;
    constexpr const char* k_uri = "file:///tmp/fix_all.hlsl";
    const std::string text = "float pow_squared(float x) { return pow(x, 2.0); }\n";
    h.did_open(k_uri, text);

    // Sanity: at least one diagnostic landed before we ask for actions.
    REQUIRE(!h.published_diagnostics_for(k_uri).empty());

    // Ask for code actions over the full document (line 0, char 0) →
    // (line 1, char 0). This is exactly what the v0.6.8 fix-all helper in the
    // VS Code extension sends.
    const auto reply = h.code_action_full_document(k_uri, /*end_line=*/1, /*end_char=*/0, 42);
    REQUIRE(reply.is_object());
    REQUIRE(reply.value("id", -1) == 42);
    REQUIRE(reply.contains("result"));
    const auto& actions = reply["result"];
    REQUIRE(actions.is_array());
    REQUIRE(!actions.empty());

    // At least one action must be a quickfix carrying a TextEdit on this URI.
    // This is the contract the fix-all gather depends on -- a regression here
    // (e.g. dropping `edit` from build_code_action) silently breaks fix-all.
    bool saw_quickfix_with_edit = false;
    for (const auto& a : actions) {
        if (a.value("kind", std::string{}) != "quickfix") {
            continue;
        }
        if (!a.contains("edit") || !a["edit"].is_object()) {
            continue;
        }
        const auto& changes = a["edit"]["changes"];
        if (!changes.is_object() || !changes.contains(k_uri)) {
            continue;
        }
        const auto& edits = changes[k_uri];
        if (!edits.is_array() || edits.empty()) {
            continue;
        }
        REQUIRE(edits[0].contains("range"));
        REQUIRE(edits[0].contains("newText"));
        // pow-to-mul rewrites `pow(x, 2.0)` to repeated multiplication of `x`.
        const auto new_text = edits[0]["newText"].get<std::string>();
        REQUIRE(new_text.find('x') != std::string::npos);
        REQUIRE(new_text != "pow(x, 2.0)");
        saw_quickfix_with_edit = true;
    }
    REQUIRE(saw_quickfix_with_edit);
}

TEST_CASE("textDocument/diagnostic (pull) is not handled — guards against "
          "re-introducing the bogus diagnosticProvider capability",
          "[lsp][handlers][regression]") {
    ServerHarness h;
    constexpr const char* k_uri = "file:///tmp/pull.hlsl";
    h.did_open(k_uri, "float f(float x) { return pow(x, 2.0); }\n");

    nlohmann::json req = nlohmann::json::object();
    req["jsonrpc"] = "2.0";
    req["id"] = 7;
    req["method"] = "textDocument/diagnostic";
    nlohmann::json p = nlohmann::json::object();
    nlohmann::json td = nlohmann::json::object();
    td["uri"] = k_uri;
    p["textDocument"] = std::move(td);
    req["params"] = std::move(p);

    const auto wire = h.dispatcher.dispatch(req);
    REQUIRE(!wire.empty());
    const auto reply = nlohmann::json::parse(wire);
    REQUIRE(reply.contains("error"));
    REQUIRE(reply["error"]["code"].get<int>() == lsp_rpc::error_code::k_method_not_found);
}
