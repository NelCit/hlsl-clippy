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

    // Per-severity counts on the active editor. When there are zero
    // diagnostics the badge collapses to just `$(check) HLSL Clippy` so
    // the status bar stays quiet for clean files; when there is at least
    // one, we render `$(error) N $(warning) M $(info) K` (omitting any
    // tier with 0 to save real estate).
    let errors = 0;
    let warnings = 0;
    let infos = 0;
    if (lastState === "ready" && vscode.window.activeTextEditor) {
        const uri = vscode.window.activeTextEditor.document.uri;
        if (uri.scheme === "file" || uri.scheme === "untitled") {
            for (const d of vscode.languages.getDiagnostics(uri)) {
                if (d.source !== undefined && d.source !== "hlsl-clippy" && d.source !== "HLSL Clippy") {
                    continue;
                }
                if (d.severity === vscode.DiagnosticSeverity.Error) errors++;
                else if (d.severity === vscode.DiagnosticSeverity.Warning) warnings++;
                else infos++;
            }
        }
    }
    const parts: string[] = [`${icon} HLSL Clippy`];
    if (lastState === "ready") {
        if (errors > 0) parts.push(`$(error) ${errors}`);
        if (warnings > 0) parts.push(`$(warning) ${warnings}`);
        if (infos > 0) parts.push(`$(info) ${infos}`);
    }
    statusBarItem.text = parts.join(" ");
    statusBarItem.tooltip =
        `${lastTooltip}\n\nClick for HLSL Clippy actions.`;
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
        // Click the badge -> quick-pick of all extension actions instead
        // of the old single jump straight to the output channel. Output
        // channel is still on the menu, just not the only option.
        statusBarItem.command = "hlslClippy.quickActions";
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
    // Quick-pick menu invoked when the user clicks the status bar badge.
    // Lists every user-facing extension command in one place; works even
    // when no HLSL file is open (commands gate themselves internally).
    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.quickActions", async () => {
            type QPItem = vscode.QuickPickItem & { command: string };
            const items: QPItem[] = [
                {
                    label: "$(refresh) Re-lint Active Document",
                    description: "Force a re-run of the linter (Ctrl+Alt+L)",
                    command: "hlslClippy.relintDocument",
                },
                {
                    label: "$(book) Open Rule Docs",
                    description: "Open the per-rule docs page for the diagnostic at the cursor (Ctrl+Alt+D)",
                    command: "hlslClippy.openRuleDocs",
                },
                {
                    label: "$(comment-discussion) Suppress Rule for This Line",
                    description: "Append `// hlsl-clippy: allow(rule)` to the current line",
                    command: "hlslClippy.suppressRuleForLine",
                },
                {
                    label: "$(file) Suppress Rule for Entire File",
                    description: "Insert a top-of-file `// hlsl-clippy: allow(rule)` directive",
                    command: "hlslClippy.suppressRuleForFile",
                },
                {
                    label: "$(list-tree) Show All Rules",
                    description: "Open the rule catalog in a side panel",
                    command: "hlslClippy.showAllRules",
                },
                {
                    label: "$(output) Show Output Channel",
                    description: "Reveal the HLSL Clippy log output",
                    command: "hlslClippy.showOutput",
                },
                {
                    label: "$(debug-restart) Restart LSP Server",
                    description: "Stop and re-spawn hlsl-clippy-lsp",
                    command: "hlslClippy.restart",
                },
                {
                    label: "$(rocket) Open Welcome Walkthrough",
                    description: "Re-open the first-install guided tour",
                    command: "hlslClippy.openWalkthrough",
                },
            ];
            const picked = await vscode.window.showQuickPick(items, {
                title: "HLSL Clippy",
                placeHolder: "Pick an action...",
            });
            if (picked) {
                await vscode.commands.executeCommand(picked.command);
            }
        }),
    );
    // Re-open the first-install walkthrough on demand. VS Code surfaces
    // walkthroughs through the Welcome page; this command offers a
    // command-palette path for users who closed the Welcome tab.
    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.openWalkthrough", async () => {
            await vscode.commands.executeCommand(
                "workbench.action.openWalkthrough",
                { category: "nelcit.hlsl-clippy#hlslClippy.gettingStarted" },
                false,
            );
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
            // The LSP server re-lints on didSave. We use the workbench
            // "files.save" command instead of `vscode.workspace.save()`
            // because the latter is VS Code 1.86+ API; our engines.vscode
            // allows 1.85+ and on 1.85 the function is undefined ->
            // TypeError. `workbench.action.files.save` has been stable
            // since 1.0 and operates on the active editor, which is the
            // editor we just verified is HLSL.
            await vscode.commands.executeCommand("workbench.action.files.save");
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

    // Suppress a single rule for one line by inserting the inline-suppression
    // comment the core understands (`// hlsl-clippy: allow(rule-id)`). When
    // invoked from the right-click menu we infer the rule id from the
    // diagnostic at the cursor; when invoked programmatically the caller
    // can pass the rule id as the first argument.
    context.subscriptions.push(
        vscode.commands.registerCommand(
            "hlslClippy.suppressRuleForLine",
            async (ruleArg?: string, lineArg?: number) => {
                const editor = vscode.window.activeTextEditor;
                if (!editor || editor.document.languageId !== k_languageId) {
                    void vscode.window.showInformationMessage(
                        "HLSL Clippy: open a .hlsl document to suppress a rule.",
                    );
                    return;
                }
                const ruleId = await resolveRuleAtCursor(editor, ruleArg);
                if (!ruleId) {
                    return;
                }
                const line = typeof lineArg === "number" ? lineArg : editor.selection.active.line;
                const lineText = editor.document.lineAt(line).text;
                // Append an inline suppression to the existing line. If the
                // line already contains a `// hlsl-clippy: allow(...)` comment
                // we extend the existing list rather than appending a second.
                const existingMatch = lineText.match(/\/\/\s*hlsl-clippy:\s*allow\(([^)]*)\)/);
                let edit: vscode.TextEdit;
                if (existingMatch) {
                    const existingIds = existingMatch[1]!.split(",").map((s) => s.trim()).filter(Boolean);
                    if (existingIds.includes(ruleId)) {
                        void vscode.window.showInformationMessage(
                            `HLSL Clippy: rule '${ruleId}' is already suppressed on this line.`,
                        );
                        return;
                    }
                    existingIds.push(ruleId);
                    const newComment = `// hlsl-clippy: allow(${existingIds.join(", ")})`;
                    const start = lineText.indexOf(existingMatch[0]);
                    const range = new vscode.Range(line, start, line, start + existingMatch[0].length);
                    edit = vscode.TextEdit.replace(range, newComment);
                } else {
                    const insertionPos = new vscode.Position(line, lineText.length);
                    edit = vscode.TextEdit.insert(
                        insertionPos,
                        `  // hlsl-clippy: allow(${ruleId})`,
                    );
                }
                const wsEdit = new vscode.WorkspaceEdit();
                wsEdit.set(editor.document.uri, [edit]);
                await vscode.workspace.applyEdit(wsEdit);
            },
        ),
    );

    // Suppress a single rule for the entire file by inserting (or extending)
    // a top-of-file `// hlsl-clippy: allow(rule-id)` directive. The same
    // inline-suppression syntax is honoured at file scope when the comment
    // sits in the first 30 lines.
    context.subscriptions.push(
        vscode.commands.registerCommand(
            "hlslClippy.suppressRuleForFile",
            async (ruleArg?: string) => {
                const editor = vscode.window.activeTextEditor;
                if (!editor || editor.document.languageId !== k_languageId) {
                    void vscode.window.showInformationMessage(
                        "HLSL Clippy: open a .hlsl document to suppress a rule.",
                    );
                    return;
                }
                const ruleId = await resolveRuleAtCursor(editor, ruleArg);
                if (!ruleId) {
                    return;
                }
                // Look at the first ~5 lines for an existing file-scope
                // allow comment; extend it rather than adding a duplicate.
                const doc = editor.document;
                const scanLines = Math.min(5, doc.lineCount);
                let extendedExisting = false;
                let edit: vscode.TextEdit | undefined;
                for (let i = 0; i < scanLines; i++) {
                    const text = doc.lineAt(i).text;
                    const m = text.match(/\/\/\s*hlsl-clippy:\s*allow\(([^)]*)\)/);
                    if (m) {
                        const ids = m[1]!.split(",").map((s) => s.trim()).filter(Boolean);
                        if (ids.includes(ruleId)) {
                            void vscode.window.showInformationMessage(
                                `HLSL Clippy: rule '${ruleId}' is already file-suppressed.`,
                            );
                            return;
                        }
                        ids.push(ruleId);
                        const start = text.indexOf(m[0]);
                        const range = new vscode.Range(i, start, i, start + m[0].length);
                        edit = vscode.TextEdit.replace(
                            range,
                            `// hlsl-clippy: allow(${ids.join(", ")})`,
                        );
                        extendedExisting = true;
                        break;
                    }
                }
                if (!edit) {
                    edit = vscode.TextEdit.insert(
                        new vscode.Position(0, 0),
                        `// hlsl-clippy: allow(${ruleId})\n`,
                    );
                }
                const wsEdit = new vscode.WorkspaceEdit();
                wsEdit.set(doc.uri, [edit]);
                await vscode.workspace.applyEdit(wsEdit);
                outputChannel?.appendLine(
                    `[hlsl-clippy] ${extendedExisting ? "Extended" : "Inserted"} file-scope allow for '${ruleId}'.`,
                );
            },
        ),
    );

    // "Show All Rules" — opens a webview listing every rule currently
    // documented under docs/rules/ on github.com. Cheap markdown-style
    // table; no Marketplace asset bloat from local doc snapshots.
    context.subscriptions.push(
        vscode.commands.registerCommand("hlslClippy.showAllRules", () => {
            const panel = vscode.window.createWebviewPanel(
                "hlslClippyRules",
                "HLSL Clippy — All Rules",
                vscode.ViewColumn.Active,
                { enableScripts: false },
            );
            panel.webview.html = renderRulesPanelHtml();
        }),
    );

    // Register a CodeActionProvider so quick-fix lightbulb (`Ctrl+.`)
    // surfaces our suppress-line / suppress-file / open-docs actions on
    // top of any quick-fix the LSP server already provides. The LSP
    // server's actions come through the language client transparently;
    // this provider supplements them.
    context.subscriptions.push(
        vscode.languages.registerCodeActionsProvider(
            { scheme: "file", language: k_languageId },
            new ClippyAuxCodeActionProvider(),
            {
                providedCodeActionKinds: [
                    vscode.CodeActionKind.QuickFix,
                ],
            },
        ),
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

/// Resolve a rule id from either an explicit caller argument or the
/// diagnostic at the active editor's cursor. Returns `undefined` and shows
/// a friendly message when the cursor is not on an HLSL Clippy diagnostic.
async function resolveRuleAtCursor(
    editor: vscode.TextEditor,
    ruleArg?: string,
): Promise<string | undefined> {
    if (typeof ruleArg === "string" && ruleArg.length > 0) {
        return ruleArg;
    }
    const diags = vscode.languages.getDiagnostics(editor.document.uri);
    const here = diags.find((d) => d.range.contains(editor.selection.active));
    if (!here || !here.code) {
        void vscode.window.showInformationMessage(
            "HLSL Clippy: place the cursor on a diagnostic squiggle, then re-run the command.",
        );
        return undefined;
    }
    const code = here.code;
    const id = typeof code === "string"
        ? code
        : typeof code === "number"
            ? String(code)
            : code.value.toString();
    if (id.startsWith("clippy::")) {
        void vscode.window.showInformationMessage(
            `HLSL Clippy: '${id}' is an engine diagnostic, not a rule -- nothing to suppress.`,
        );
        return undefined;
    }
    return id;
}

/// Code-action provider that augments LSP-provided quick-fixes with the
/// three workflow shortcuts: open the rule's docs page, suppress the rule
/// for the current line, suppress it for the whole file. Each action is
/// a thin wrapper that invokes the corresponding command above.
class ClippyAuxCodeActionProvider implements vscode.CodeActionProvider {
    provideCodeActions(
        document: vscode.TextDocument,
        range: vscode.Range | vscode.Selection,
        context: vscode.CodeActionContext,
    ): vscode.CodeAction[] {
        const out: vscode.CodeAction[] = [];
        for (const diag of context.diagnostics) {
            // Only act on diagnostics that look like ours (rule id is a
            // hyphen-separated lowercase token; engine diagnostics start
            // with `clippy::` and are not user-suppressible).
            const code = diag.code;
            if (code === undefined) {
                continue;
            }
            const ruleId = typeof code === "string"
                ? code
                : typeof code === "number"
                    ? String(code)
                    : code.value.toString();
            if (ruleId.startsWith("clippy::")) {
                continue;
            }

            const lineActions = (label: string, command: string, args: unknown[]): vscode.CodeAction => {
                const a = new vscode.CodeAction(label, vscode.CodeActionKind.QuickFix);
                a.command = { command, title: label, arguments: args };
                a.diagnostics = [diag];
                return a;
            };

            out.push(
                lineActions(
                    `HLSL Clippy: suppress '${ruleId}' for this line`,
                    "hlslClippy.suppressRuleForLine",
                    [ruleId, range.start.line],
                ),
                lineActions(
                    `HLSL Clippy: suppress '${ruleId}' for entire file`,
                    "hlslClippy.suppressRuleForFile",
                    [ruleId],
                ),
                lineActions(
                    `HLSL Clippy: open '${ruleId}' docs`,
                    "hlslClippy.openRuleDocs",
                    [ruleId],
                ),
            );
        }
        return out;
    }
}

/// Render the static HTML for the "Show All Rules" webview. Lists rule
/// categories with a deep link to docs/rules/<rule>.md on github.com.
/// Pure-CSS table; no JS, no extra Marketplace asset weight.
function renderRulesPanelHtml(): string {
    const categories = [
        ["math", "Math intrinsics, transcendentals, packed-math"],
        ["bindings", "cbuffer, root signature, descriptor heap, UAV/SRV"],
        ["texture", "Sampler state, format/swizzle, sample-LOD"],
        ["workgroup", "[numthreads], groupshared / LDS bank patterns"],
        ["control-flow", "Branch attributes, divergent CF, loop hints"],
        ["wave-helper-lane", "Wave intrinsics + helper-lane semantics"],
        ["mesh", "Mesh shader output topology + numthreads"],
        ["dxr", "DXR ray flags, payload, hit shaders"],
        ["work-graphs", "Mesh nodes, output topology, OutputComplete"],
        ["ser", "SM 6.9 shader execution reordering"],
        ["cooperative-vector", "SM 6.9 coopvec layout / stride / handles"],
        ["long-vectors", "SM 6.9 long-vector buffer + signature rules"],
        ["opacity-micromaps", "SM 6.9 OMM 2-state / pipeline flags"],
        ["sampler-feedback", "SamplerFeedback streaming flags + stages"],
        ["vrs", "VRS shading-rate output compatibility"],
    ];
    const rows = categories
        .map(([id, blurb]) => `
            <tr>
              <td><a href="https://github.com/NelCit/hlsl-clippy/tree/main/docs/rules#${id}">${id}</a></td>
              <td>${blurb}</td>
            </tr>`)
        .join("");
    return `
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline';" />
  <title>HLSL Clippy — All Rules</title>
  <style>
    body { font-family: var(--vscode-font-family); padding: 16px; line-height: 1.5; }
    h1 { font-size: 20px; margin-bottom: 8px; }
    p.lead { color: var(--vscode-descriptionForeground); margin-top: 0; }
    table { width: 100%; border-collapse: collapse; margin-top: 16px; }
    th, td { padding: 8px 12px; text-align: left; border-bottom: 1px solid var(--vscode-widget-border, #444); }
    th { font-weight: 600; }
    a { color: var(--vscode-textLink-foreground); }
    a:hover { color: var(--vscode-textLink-activeForeground); }
    code { background: var(--vscode-textCodeBlock-background); padding: 1px 4px; border-radius: 3px; }
  </style>
</head>
<body>
  <h1>HLSL Clippy — Rule Catalog</h1>
  <p class="lead">
    154 rules ship in v0.6.x across 15 categories. Click a category to jump
    to its section in the docs site. Per-rule pages are at
    <code>docs/rules/&lt;rule-id&gt;.md</code> on GitHub.
  </p>
  <table>
    <thead>
      <tr><th>Category</th><th>Scope</th></tr>
    </thead>
    <tbody>
      ${rows}
    </tbody>
  </table>
  <p style="margin-top:24px;">
    Tip: <code>HLSL Clippy: Open Rule Docs</code>
    (<code>Ctrl+Alt+D</code> / <code>Cmd+Alt+D</code>) opens the per-rule
    page for the diagnostic at the cursor.
  </p>
</body>
</html>`;
}
