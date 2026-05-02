// Smoke test the LSP from a different CWD (mimics how VS Code spawns
// it -- workspace folder, not the EXE directory). Catches the case
// where Windows DLL search order doesn't pick up sibling DLLs from
// the EXE's directory because of safe-search mode or PATH overrides.
//
// Usage: node tools/smoke-vsix.js <path-to-extracted-extension>
//   e.g. node tools/smoke-vsix.js /tmp/v066_test/extension

const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");
const os = require("os");

const extensionRoot = process.argv[2];
if (!extensionRoot) {
    console.error("usage: node tools/smoke-vsix.js <extension-root>");
    process.exit(1);
}

const platform =
    process.platform === "win32"  ? "windows-x86_64"
    : process.platform === "darwin" ? (process.arch === "arm64" ? "macos-aarch64" : "macos-x86_64")
    : "linux-x86_64";
const exeName = process.platform === "win32" ? "hlsl-clippy-lsp.exe" : "hlsl-clippy-lsp";
const lsp = path.join(extensionRoot, "server", platform, exeName);

if (!fs.existsSync(lsp)) {
    console.error(`LSP not found at ${lsp}`);
    process.exit(1);
}

// Use a CWD that has no DLLs near it -- mimics what VS Code does when
// the workspace folder is unrelated to the extension install dir.
const fakeCwd = path.join(os.tmpdir(), "hlsl-clippy-cwd-test");
fs.mkdirSync(fakeCwd, { recursive: true });

const fixtureSrc = path.join(__dirname, "..", "tests", "fixtures", "phase2", "math.hlsl");
const text = fs.readFileSync(fixtureSrc, "utf-8");

console.log(`LSP   : ${lsp}`);
console.log(`CWD   : ${fakeCwd}  (different dir than EXE; mimics VS Code spawn)`);
console.log("");

function frame(body) {
    const payload = Buffer.from(body, "utf-8");
    return Buffer.concat([
        Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, "ascii"),
        payload,
    ]);
}

const proc = spawn(lsp, [], { cwd: fakeCwd, stdio: ["pipe", "pipe", "pipe"] });
console.log(`PID ${proc.pid} spawned.`);

proc.on("error", (e) => console.log(`spawn error: ${e.message}`));
proc.on("exit", (code) => console.log(`exit code: ${code}`));

const frames = [];
let buf = Buffer.alloc(0);
proc.stdout.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    while (true) {
        const headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) break;
        const headers = buf.subarray(0, headerEnd).toString("ascii");
        const m = headers.match(/Content-Length:\s*(\d+)/i);
        if (!m) { buf = buf.subarray(headerEnd + 4); continue; }
        const len = parseInt(m[1], 10);
        const bodyStart = headerEnd + 4;
        if (buf.length < bodyStart + len) break;
        const body = buf.subarray(bodyStart, bodyStart + len).toString("utf-8");
        try { frames.push(JSON.parse(body)); } catch (e) {}
        buf = buf.subarray(bodyStart + len);
    }
});

const stderrChunks = [];
proc.stderr.on("data", (c) => stderrChunks.push(c));

(async () => {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const uri = "file:///" + fixtureSrc.replace(/\\/g, "/");

    proc.stdin.write(frame(JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize",
        params: { processId: process.pid, rootUri: null, capabilities: {} } })));
    await sleep(700);
    proc.stdin.write(frame(JSON.stringify({ jsonrpc: "2.0", method: "initialized", params: {} })));
    await sleep(100);
    proc.stdin.write(frame(JSON.stringify({ jsonrpc: "2.0", method: "textDocument/didOpen",
        params: { textDocument: { uri, languageId: "hlsl", version: 1, text } } })));
    await sleep(2500);
    proc.stdin.write(frame(JSON.stringify({ jsonrpc: "2.0", id: 2, method: "shutdown", params: null })));
    await sleep(200);
    proc.stdin.write(frame(JSON.stringify({ jsonrpc: "2.0", method: "exit", params: null })));
    proc.stdin.end();

    await new Promise((r) => setTimeout(r, 1500));
    if (proc.exitCode === null) { proc.kill(); await new Promise((r) => setTimeout(r, 200)); }

    const initOk = frames.some((f) => f.id === 1 && f.result);
    console.log(`initialize: ${initOk ? "OK" : "NO RESPONSE"}`);
    const diagFrames = frames.filter((f) => f.method === "textDocument/publishDiagnostics");
    let total = 0;
    for (const fr of diagFrames) total += (fr.params.diagnostics || []).length;
    console.log(`publishDiagnostics frames: ${diagFrames.length} (${total} diagnostics)`);

    const stderrTxt = Buffer.concat(stderrChunks).toString("utf-8");
    if (stderrTxt.trim()) {
        console.log("\n--- stderr ---");
        console.log(stderrTxt);
    }
    process.exit(total > 0 ? 0 : 3);
})();
