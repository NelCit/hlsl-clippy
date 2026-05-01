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
        // Binary I/O matters on Windows: ensure no CRLF translation on the
        // framed stream. nlohmann/json round-trips raw bytes, but the
        // framing layer expects exact byte counts to match Content-Length.
#if defined(_WIN32)
        std::ios_base::sync_with_stdio(false);
        // Note: switching stdin/stdout to binary mode is platform-specific
        // and usually done via _setmode in production. For sub-phase 5a,
        // leave it text-mode; VS Code's vscode-languageclient writes raw
        // bytes either way and Windows CRLF translation only flips
        // standalone `\n` writes that we never produce in framed bodies.
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
