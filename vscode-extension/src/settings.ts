// Copyright 2026 NelCit
// SPDX-License-Identifier: Apache-2.0
//
// Strongly-typed accessor over `vscode.workspace.getConfiguration("shaderClippy")`.
// Bridges VS Code settings to the LSP `initializationOptions` payload that
// `lsp/src/server/handlers.cpp` consumes (per ADR 0014 §6).

import * as vscode from "vscode";

export type TraceLevel = "off" | "messages" | "verbose";

export interface ClippySettings {
    /** Explicit override for the LSP server binary path. Empty = auto-discover. */
    readonly serverPath: string;
    /** Slang target profile forwarded to `LintOptions::target_profile`. */
    readonly targetProfile: string;
    /** Extra include roots forwarded to Slang reflection. */
    readonly includeDirectories: readonly string[];
    /** Phase 3 toggle — `LintOptions::enable_reflection`. */
    readonly enableReflection: boolean;
    /** Phase 4 toggle — `LintOptions::enable_control_flow`. */
    readonly enableControlFlow: boolean;
    /** vscode-languageclient trace level. */
    readonly trace: TraceLevel;
}

const k_section = "shaderClippy";

export function readSettings(): ClippySettings {
    const cfg = vscode.workspace.getConfiguration(k_section);
    return {
        serverPath: cfg.get<string>("serverPath", "").trim(),
        targetProfile: cfg.get<string>("targetProfile", "").trim(),
        includeDirectories: expandIncludeDirectories(
            cfg.get<string[]>("includeDirectories", []),
        ),
        enableReflection: cfg.get<boolean>("enableReflection", true),
        enableControlFlow: cfg.get<boolean>("enableControlFlow", true),
        trace: getTraceLevel(),
    };
}

function expandIncludeDirectories(raw: readonly string[]): string[] {
    const folders = vscode.workspace.workspaceFolders ?? [];
    const out: string[] = [];
    const seen = new Set<string>();
    const push = (value: string): void => {
        const trimmed = value.trim();
        if (trimmed.length === 0 || seen.has(trimmed)) {
            return;
        }
        seen.add(trimmed);
        out.push(trimmed);
    };

    for (const entry of raw) {
        if (typeof entry !== "string") {
            continue;
        }
        if (entry.includes("${workspaceFolder}") && folders.length > 0) {
            for (const folder of folders) {
                push(entry.replaceAll("${workspaceFolder}", folder.uri.fsPath));
            }
        } else {
            push(entry);
        }
    }
    return out;
}

export function getTraceLevel(): TraceLevel {
    const cfg = vscode.workspace.getConfiguration(k_section);
    const raw = cfg.get<string>("trace.server", "off");
    if (raw === "messages" || raw === "verbose") {
        return raw;
    }
    return "off";
}

/**
 * Map settings to the `initializationOptions` payload sent to the LSP server.
 * Field naming intentionally matches the JSON keys the server expects so the
 * C++ side does not need to translate.
 */
export function toInitializationOptions(settings: ClippySettings): Record<string, unknown> {
    const opts: Record<string, unknown> = {
        enableReflection: settings.enableReflection,
        enableControlFlow: settings.enableControlFlow,
        includeDirectories: settings.includeDirectories,
    };
    if (settings.targetProfile.length > 0) {
        opts.targetProfile = settings.targetProfile;
    }
    return opts;
}
