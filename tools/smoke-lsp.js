// End-to-end smoke test of shader-clippy-lsp.exe over JSON-RPC stdio.
// Sends initialize -> initialized -> didOpen -> shutdown -> exit and
// prints any publishDiagnostics frame the server emits. Exit 0 only if
// at least one diagnostic is published.
//
// Catches both classes of LSP-startup failure:
//   - Missing Slang DLLs (server crashes before stdin handshake).
//   - Text-mode stdio CRLF mangling on Windows (parser hangs forever).

const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const lsp = path.join(root, "build", "lsp", "shader-clippy-lsp.exe");
const fixture = path.join(root, "tests", "fixtures", "phase2", "math.hlsl");

if (!fs.existsSync(lsp)) {
    console.log(`FAIL: LSP binary not found at ${lsp}`);
    process.exit(1);
}
if (!fs.existsSync(fixture)) {
    console.log(`FAIL: fixture not found at ${fixture}`);
    process.exit(1);
}

function frame(body) {
    const payload = Buffer.from(body, "utf-8");
    return Buffer.concat([
        Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, "ascii"),
        payload,
    ]);
}

const proc = spawn(lsp, [], {
    cwd: path.dirname(lsp),  // Windows DLL search includes the exe's dir.
    stdio: ["pipe", "pipe", "pipe"],
});
console.log(`PID ${proc.pid} running.`);

const frames = [];
let buf = Buffer.alloc(0);

proc.stdout.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    while (true) {
        const headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) break;
        const headers = buf.subarray(0, headerEnd).toString("ascii");
        const m = headers.match(/Content-Length:\s*(\d+)/i);
        if (!m) {
            buf = buf.subarray(headerEnd + 4);
            continue;
        }
        const len = parseInt(m[1], 10);
        const bodyStart = headerEnd + 4;
        if (buf.length < bodyStart + len) break;
        const body = buf.subarray(bodyStart, bodyStart + len).toString("utf-8");
        try {
            frames.push(JSON.parse(body));
        } catch (e) {
            console.log(`  [parse error] ${e.message}: ${body.slice(0, 100)}`);
        }
        buf = buf.subarray(bodyStart + len);
    }
});

const stderrChunks = [];
proc.stderr.on("data", (chunk) => stderrChunks.push(chunk));

const text = fs.readFileSync(fixture, "utf-8");
const uri = "file:///" + fixture.replace(/\\/g, "/");

const init = { jsonrpc: "2.0", id: 1, method: "initialize",
    params: { processId: process.pid, rootUri: null, capabilities: {} } };
const inited = { jsonrpc: "2.0", method: "initialized", params: {} };
const opened = { jsonrpc: "2.0", method: "textDocument/didOpen",
    params: { textDocument: { uri, languageId: "hlsl", version: 1, text } } };
const shutdown = { jsonrpc: "2.0", id: 2, method: "shutdown", params: null };
const exit_msg = { jsonrpc: "2.0", method: "exit", params: null };

(async () => {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

    console.log("==> initialize");
    proc.stdin.write(frame(JSON.stringify(init)));
    await sleep(500);

    proc.stdin.write(frame(JSON.stringify(inited)));
    await sleep(100);

    console.log(`==> didOpen ${path.basename(fixture)} (${text.length} bytes)`);
    proc.stdin.write(frame(JSON.stringify(opened)));
    await sleep(2000);  // Allow lint pipeline to run.

    proc.stdin.write(frame(JSON.stringify(shutdown)));
    await sleep(200);
    proc.stdin.write(frame(JSON.stringify(exit_msg)));
    proc.stdin.end();

    await new Promise((r) => setTimeout(r, 1500));
    if (!proc.killed && proc.exitCode === null) {
        console.log("WARN: server still alive after exit; killing.");
        proc.kill();
        await new Promise((r) => setTimeout(r, 200));
    }

    const initOk = frames.some((f) => f.id === 1 && f.result);
    console.log(`  initialize -> ${initOk ? "OK" : "NO RESPONSE"}`);

    const diagFrames = frames.filter((f) => f.method === "textDocument/publishDiagnostics");
    console.log(`  publishDiagnostics frames: ${diagFrames.length}`);

    let total = 0;
    for (const fr of diagFrames) {
        const diags = (fr.params && fr.params.diagnostics) || [];
        total += diags.length;
        for (const d of diags.slice(0, 8)) {
            const sev = { 1: "error", 2: "warn ", 3: "info ", 4: "hint " }[d.severity] || "?    ";
            const line = d.range.start.line + 1;
            const col = d.range.start.character + 1;
            const msg = (d.message || "").split("\n")[0].slice(0, 80);
            const code = d.code || "?";
            console.log(`    [${sev}] ${String(line).padStart(3)}:${String(col).padEnd(3)} ${String(code).padEnd(40)} ${msg}`);
        }
        if (diags.length > 8) {
            console.log(`    ... and ${diags.length - 8} more in this frame`);
        }
    }

    const stderrTxt = Buffer.concat(stderrChunks).toString("utf-8");
    if (stderrTxt.trim()) {
        console.log("\n----- stderr -----");
        console.log(stderrTxt);
    }

    if (total === 0) {
        console.log("\nFAIL: no diagnostics emitted.");
        process.exit(3);
    }
    console.log(`\nPASS: ${total} diagnostic(s) emitted across ${diagFrames.length} frame(s).`);
    process.exit(0);
})();
