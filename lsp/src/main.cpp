// hlsl-clippy-lsp entry point.
//
// stdio transport (the canonical transport VS Code uses to spawn LSP
// servers). Read framed JSON-RPC messages from stdin; dispatch via
// `JsonRpcDispatcher`; write framed responses + server-initiated
// notifications back to stdout. Exit cleanly on `exit` notification.
//
// Per ADR 0014: no socket support in 5a.

#include <exception>
#include <iostream>
#include <ostream>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include <nlohmann/json.hpp>

#include "document/manager.hpp"
#include "rpc/dispatcher.hpp"
#include "rpc/framing.hpp"
#include "server/handlers.hpp"

namespace {

void write_framed(std::ostream& out, const std::string& body) {
    out << hlsl_clippy::lsp::rpc::frame_message(body);
    out.flush();
}

[[nodiscard]] int run_server(std::istream& in, std::ostream& out) {
    hlsl_clippy::lsp::rpc::JsonRpcDispatcher dispatcher;
    hlsl_clippy::lsp::document::DocumentManager docs;

    auto sink = [&out](const nlohmann::json& envelope) { write_framed(out, envelope.dump()); };

    hlsl_clippy::lsp::server::Server server(dispatcher, docs, sink);
    server.register_handlers();

    while (!server.should_exit()) {
        const auto msg = hlsl_clippy::lsp::rpc::read_message(in);
        if (msg.status == hlsl_clippy::lsp::rpc::FramingStatus::EndOfStream) {
            // Lost stdin without a clean `exit` — treat as abnormal exit per
            // LSP §"exit Notification": exit code 1 unless `shutdown` was
            // already received.
            break;
        }
        if (msg.status != hlsl_clippy::lsp::rpc::FramingStatus::Ok) {
            // Framing error — ignore the malformed message and try to
            // continue. A persistently broken stream will eventually hit
            // the EndOfStream branch above.
            continue;
        }
        const auto reply = dispatcher.dispatch_wire(msg.body);
        if (!reply.empty()) {
            write_framed(out, reply);
        }
    }

    return server.exit_code();
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape) -- catch-all guarantees no escape.
int main() {
    try {
        // Binary I/O is mandatory on Windows. The LSP framing layer parses
        // `Content-Length: N\r\n\r\n<body>` and reads exactly N bytes; in
        // text mode, Windows silently maps `\r\n` -> `\n` on stdin reads
        // (so the framing terminator never matches anything that arrives
        // from the wire) and `\n` -> `\r\n` on stdout writes (corrupting
        // every Content-Length byte count). Both directions break LSP
        // simultaneously. Symptom: server hangs immediately after spawn,
        // VS Code's Problems panel stays empty, no error is logged.
        //
        // Pre-v0.6.6 this file kept text mode with a comment claiming
        // "VS Code's vscode-languageclient writes raw bytes either way" --
        // that comment was wrong. The languageclient writes per-spec
        // CRLF-framed headers and our parser searches for `\r\n\r\n`.
        // Without _setmode the LSP has been broken on Windows since v0.5.0.
#if defined(_WIN32)
        std::ios_base::sync_with_stdio(false);
        if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
            std::cerr << "hlsl-clippy-lsp: failed to switch stdin to binary mode\n";
            return 1;
        }
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
            std::cerr << "hlsl-clippy-lsp: failed to switch stdout to binary mode\n";
            return 1;
        }
#endif
        return run_server(std::cin, std::cout);
    } catch (const std::exception& ex) {
        std::cerr << "hlsl-clippy-lsp: fatal: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "hlsl-clippy-lsp: fatal: unknown exception\n";
        return 1;
    }
}
