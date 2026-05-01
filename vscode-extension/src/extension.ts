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

const k_languageId = "hlsl";
const k_clientId = "hlslClippy";
const k_clientName = "HLSL Clippy";

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    outputChannel = vscode.window.createOutputChannel(k_clientName);
    context.subscriptions.push(outputChannel);

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
    } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        outputChannel.appendLine(`[hlsl-clippy] Failed to start: ${message}`);
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
    if (client !== undefined && client.state !== State.Stopped) {
        await client.stop();
        client = undefined;
    }
    try {
        await startServer(context);
    } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
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
