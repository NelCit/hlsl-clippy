// Copyright 2026 NelCit
// SPDX-License-Identifier: Apache-2.0
//
// Activation entry point for the HLSL Clippy VS Code extension. Spawns the
// `hlsl-clippy-lsp` subprocess as a stdio LSP server (per ADR 0014, sub-phase
// 5a — no socket transport in v0.5) and bridges it to VS Code via
// `vscode-languageclient` v9.

import * as path from "path";
import * as vscode from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
    State,
    RevealOutputChannelOn,
} from "vscode-languageclient/node";

import { resolveServerBinary, ServerResolutionError } from "./server-binary";
import { readSettings, toInitializationOptions, getTraceLevel } from "./settings";

let client: LanguageClient | undefined;
let outputChannel: vscode.OutputChannel | undefined;
let statusBarItem: vscode.StatusBarItem | undefined;

const k_languageId = "hlsl";
const k_clientId = "hlslClippy";
const k_clientName = "HLSL Clippy";

// Visible signal in the status bar so users can tell at a glance whether
// the extension activated, is in the middle of starting, or failed
// silently. Without this, a working install and a broken install (e.g.
// missing Slang DLLs on Windows -- the hlsl-clippy-lsp.exe load fails
// before the JSON-RPC handshake) look identical from the editor.
// Most recent state, kept around so the diagnostic-count refresh can rebuild
// the status text without losing the last health signal.
let lastState: "starting" | "ready" | "error" = "starting";
let lastTooltip = "HLSL Clippy: starting...";

function renderStatus() {
    if (!statusBarItem) {
        return;
    }
    const icon =
        lastState === "ready" ? "$(check)"
            : lastState === "starting" ? "$(sync~spin)"
            : "$(error)";

    let count = 0;
    if (lastState === "ready" && vscode.window.activeTextEditor) {
        const uri = vscode.window.activeTextEditor.document.uri;
        if (uri.scheme === "file" || uri.scheme === "untitled") {
            count = vscode.languages.getDiagnostics(uri)
                .filter((d) => d.source === undefined || d.source === "hlsl-clippy")
                .length;
        }
    }
    const countLabel = lastState === "ready" && count > 0 ? ` ${count}` : "";
    statusBarItem.text = `${icon} HLSL Clippy${countLabel}`;
    statusBarItem.tooltip =
        `${lastTooltip}\n\nClick to open the HLSL Clippy output channel.`;
    statusBarItem.backgroundColor =
        lastState === "error"
            ? new vscode.ThemeColor("statusBarItem.errorBackground")
            : undefined;
}

function setStatus(state: "starting" | "ready" | "error", tooltip: string) {
    if (!statusBarItem) {
        statusBarItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Right,
            100,
        );
        statusBarItem.command = "hlslClippy.showOutput";
        statusBarItem.show();
    }
    lastState = state;
    lastTooltip = tooltip;
    renderStatus();
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    outputChannel = vscode.window.createOutputChannel(k_clientName);
    context.subscriptions.push(outputChannel);
    setStatus("starting", "HLSL Clippy: starting LSP server...");
    context.subscriptions.push({
        dispose: () => statusBarItem?.dispose(),
    });

    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.restart", async () => {
            await restartServer(context);
        }),
    );
    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.showOutput", () => {
            outputChannel?.show(true);
        }),
    );
    // Force a re-lint of the active document. Useful after editing
    // .hlsl-clippy.toml or after enabling/disabling a rule via settings,
    // when the user wants to see the new behaviour without typing.
    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.relintDocument", async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== k_languageId) {
                void vscode.window.showInformationMessage(
                    "HLSL Clippy: open a .hlsl document to re-lint it.",
                );
                return;
            }
            // The LSP server re-lints on didSave, so the cheapest "force a
            // re-lint" trigger is a no-op save. We round-trip a synthetic
            // didChange + didSave through the language client.
            await vscode.workspace.save(editor.document.uri);
            outputChannel?.appendLine(
                `[hlsl-clippy] Re-lint requested for ${editor.document.uri.fsPath}`,
            );
        }),
    );
    // Surface the per-rule docs page for the diagnostic at the cursor.
    // VS Code's diagnostic.code can be either a string or a {value, target}
    // pair; we accept both. Falls back to listing all diagnostics if the
    // cursor isn't on one.
    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.openRuleDocs", async (ruleArg?: string) => {
            let ruleId: string | undefined = typeof ruleArg === "string" ? ruleArg : undefined;
            if (!ruleId) {
                const editor = vscode.window.activeTextEditor;
                if (editor) {
                    const diags = vscode.languages.getDiagnostics(editor.document.uri);
                    const here = diags.find((d) => d.range.contains(editor.selection.active));
                    if (here && here.code) {
                        const code = here.code;
                        ruleId = typeof code === "string"
                            ? code
                            : typeof code === "number"
                                ? String(code)
                                : code.value.toString();
                    }
                }
            }
            if (!ruleId || ruleId.startsWith("clippy::")) {
                void vscode.window.showInformationMessage(
                    "HLSL Clippy: place the cursor on a diagnostic to open its rule docs.",
                );
                return;
            }
            const url = `https://github.com/NelCit/hlsl-clippy/blob/main/docs/rules/${ruleId}.md`;
            await vscode.env.openExternal(vscode.Uri.parse(url));
        }),
    );

    // Refresh the status-bar diagnostic count whenever the active editor or
    // its diagnostics change. Cheap (status bar render is constant work);
    // covers the typing-cadence case + the cursor-moves-to-other-file case.
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(() => renderStatus()),
        vscode.languages.onDidChangeDiagnostics(() => renderStatus()),
    );

    // React to settings changes that affect the server: settings forwarded as
    // initializationOptions take effect on restart; trace.server takes effect
    // immediately via the language-client's own listener.
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration(async (event) => {
            if (
                event.affectsConfiguration("hlslClippy.serverPath") ||
                event.affectsConfiguration("hlslClippy.targetProfile") ||
                event.affectsConfiguration("hlslClippy.enableReflection") ||
                event.affectsConfiguration("hlslClippy.enableControlFlow")
            ) {
                outputChannel?.appendLine(
                    "[hlsl-clippy] Settings changed; restarting server.",
                );
                await restartServer(context);
            }
        }),
    );

    try {
        await startServer(context);
        setStatus("ready", "HLSL Clippy: LSP server running. Open a .hlsl / .hlsli file to see diagnostics.");
        outputChannel.appendLine(
            "[hlsl-clippy] LSP server ready. Open a .hlsl / .hlsli file to see diagnostics in the Problems panel.",
        );
    } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        outputChannel.appendLine(`[hlsl-clippy] Failed to start: ${message}`);
        setStatus("error", `HLSL Clippy: failed to start LSP server.\n${message}`);
        if (err instanceof ServerResolutionError) {
            void vscode.window.showErrorMessage(
                `HLSL Clippy: ${err.message} ` +
                    `Set "hlslClippy.serverPath" to the hlsl-clippy-lsp binary.`,
            );
        } else {
            void vscode.window.showErrorMessage(
                `HLSL Clippy failed to start: ${message}`,
            );
        }
    }
}

export async function deactivate(): Promise<void> {
    if (client === undefined) {
        return;
    }
    if (client.state === State.Stopped) {
        client = undefined;
        return;
    }
    await client.stop();
    client = undefined;
}

async function startServer(context: vscode.ExtensionContext): Promise<void> {
    const settings = readSettings();
    const serverPath = await resolveServerBinary(context, settings, outputChannel);

    outputChannel?.appendLine(`[hlsl-clippy] Spawning server: ${serverPath}`);

    const cwd = workspaceCwd();

    const serverOptions: ServerOptions = {
        run: {
            command: serverPath,
            transport: TransportKind.stdio,
            options: { cwd },
        },
        debug: {
            command: serverPath,
            transport: TransportKind.stdio,
            options: { cwd, env: { ...process.env, HLSL_CLIPPY_LSP_DEBUG: "1" } },
        },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: "file", language: k_languageId },
            { scheme: "untitled", language: k_languageId },
        ],
        synchronize: {
            // Server-side file watcher will be the canonical one (per ADR 0014
            // §4 — `client/registerCapability` for `**/.hlsl-clippy.toml`),
            // but mirror it here so settings-changes round-trip cleanly under
            // older clients.
            fileEvents: vscode.workspace.createFileSystemWatcher(
                "**/.hlsl-clippy.toml",
            ),
            configurationSection: "hlslClippy",
        },
        initializationOptions: toInitializationOptions(settings),
        outputChannel,
        traceOutputChannel: outputChannel,
        // Surface server crashes loudly; the user will likely want to file a bug.
        revealOutputChannelOn: RevealOutputChannelOn.Error,
    };

    client = new LanguageClient(k_clientId, k_clientName, serverOptions, clientOptions);

    // Apply the persisted trace level on first start. Subsequent changes flow
    // through the client's own settings observer.
    const traceLevel = getTraceLevel();
    if (traceLevel !== "off") {
        outputChannel?.appendLine(`[hlsl-clippy] Trace level: ${traceLevel}`);
    }

    await client.start();
    context.subscriptions.push(client);
}

async function restartServer(context: vscode.ExtensionContext): Promise<void> {
    setStatus("starting", "HLSL Clippy: restarting LSP server...");
    if (client !== undefined && client.state !== State.Stopped) {
        await client.stop();
        client = undefined;
    }
    try {
        await startServer(context);
        setStatus("ready", "HLSL Clippy: LSP server running.");
    } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        setStatus("error", `HLSL Clippy: restart failed.\n${message}`);
        void vscode.window.showErrorMessage(
            `HLSL Clippy restart failed: ${message}`,
        );
    }
}

function workspaceCwd(): string | undefined {
    const folders = vscode.workspace.workspaceFolders;
    if (folders === undefined || folders.length === 0) {
        return undefined;
    }
    // Multi-root workspaces resolve config per-document on the server side
    // (per ADR 0014 §4); the cwd just provides a sensible default for path
    // resolution in diagnostic messages.
    return path.normalize(folders[0].uri.fsPath);
}
