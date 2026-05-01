// Copyright 2026 NelCit
// SPDX-License-Identifier: Apache-2.0
//
// Locate (and lazily download) the `hlsl-clippy-lsp` binary the extension
// will spawn. Resolution order per ADR 0014 §6:
//
//   1. `hlslClippy.serverPath` setting (if set + file exists).
//   2. `hlsl-clippy-lsp[.exe]` on `process.env.PATH`.
//   3. Bundled binary at `<extension_root>/server/<platform>/hlsl-clippy-lsp[.exe]`.
//   4. Cached download at `<global_storage>/hlsl-clippy-lsp-<version>/...`.
//   5. Trigger download (via `download.ts`).
//
// As of v0.5.3 the Marketplace ships per-platform .vsix files that bundle
// the LSP binary at step 3 — every Marketplace install hits step 3 with no
// network access required. Steps 4 + 5 (cache + download) survive only as
// fallbacks for the rare cases where a sideloaded .vsix is missing the
// bundled binary, or where the user explicitly cleared the bundled path.

import { promises as fs } from "fs";
import * as os from "os";
import * as path from "path";
import * as vscode from "vscode";

import { ClippySettings } from "./settings";
import { downloadServerBinary, currentPlatform } from "./download";

export class ServerResolutionError extends Error {
    constructor(message: string) {
        super(message);
        this.name = "ServerResolutionError";
    }
}

const k_binaryBaseName = "hlsl-clippy-lsp";

function binaryName(): string {
    return process.platform === "win32" ? `${k_binaryBaseName}.exe` : k_binaryBaseName;
}

async function fileExists(filepath: string): Promise<boolean> {
    try {
        const st = await fs.stat(filepath);
        return st.isFile();
    } catch {
        return false;
    }
}

async function isExecutable(filepath: string): Promise<boolean> {
    if (!(await fileExists(filepath))) {
        return false;
    }
    if (process.platform === "win32") {
        return true;
    }
    try {
        await fs.access(filepath, fs.constants.X_OK);
        return true;
    } catch {
        return false;
    }
}

async function findOnPath(): Promise<string | undefined> {
    const pathEnv = process.env.PATH;
    if (pathEnv === undefined || pathEnv.length === 0) {
        return undefined;
    }
    const sep = process.platform === "win32" ? ";" : ":";
    const exe = binaryName();
    for (const dir of pathEnv.split(sep)) {
        if (dir.length === 0) {
            continue;
        }
        const candidate = path.join(dir, exe);
        if (await isExecutable(candidate)) {
            return candidate;
        }
    }
    return undefined;
}

async function findBundled(context: vscode.ExtensionContext): Promise<string | undefined> {
    const platformDir = currentPlatform();
    if (platformDir === undefined) {
        return undefined;
    }
    const candidate = path.join(
        context.extensionPath,
        "server",
        platformDir,
        binaryName(),
    );
    if (await isExecutable(candidate)) {
        return candidate;
    }
    return undefined;
}

async function findCached(
    context: vscode.ExtensionContext,
    version: string,
): Promise<string | undefined> {
    const storage = context.globalStorageUri.fsPath;
    const candidate = path.join(
        storage,
        `${k_binaryBaseName}-${version}`,
        binaryName(),
    );
    if (await isExecutable(candidate)) {
        return candidate;
    }
    return undefined;
}

function extensionVersion(context: vscode.ExtensionContext): string {
    const manifest = context.extension.packageJSON as { version?: string };
    return manifest.version ?? "0.0.0";
}

/**
 * Resolve the LSP server binary path, falling through the priority chain
 * defined in ADR 0014 §6. Throws `ServerResolutionError` with a user-facing
 * message if none of the strategies succeed.
 */
export async function resolveServerBinary(
    context: vscode.ExtensionContext,
    settings: ClippySettings,
    output: vscode.OutputChannel | undefined,
): Promise<string> {
    const log = (msg: string) => output?.appendLine(`[hlsl-clippy] ${msg}`);

    // 1. Explicit override.
    if (settings.serverPath.length > 0) {
        const explicit = path.normalize(settings.serverPath);
        if (await isExecutable(explicit)) {
            log(`Using hlslClippy.serverPath: ${explicit}`);
            return explicit;
        }
        throw new ServerResolutionError(
            `hlslClippy.serverPath points to "${explicit}" but the file is not executable.`,
        );
    }

    // 2. PATH lookup.
    const onPath = await findOnPath();
    if (onPath !== undefined) {
        log(`Found ${binaryName()} on PATH: ${onPath}`);
        return onPath;
    }

    // 3. Bundled per-platform binary (if a future .vsix ships one).
    const bundled = await findBundled(context);
    if (bundled !== undefined) {
        log(`Using bundled server binary: ${bundled}`);
        return bundled;
    }

    // 4. Cached download.
    const version = extensionVersion(context);
    const cached = await findCached(context, version);
    if (cached !== undefined) {
        log(`Using cached server binary: ${cached}`);
        return cached;
    }

    // 5. Download from GitHub Releases.
    //
    // IMPORTANT: Until sub-phase 5e ships per-platform release artifacts,
    // this step will fail. Surface a clear error so the user knows to
    // configure hlslClippy.serverPath manually.
    log(
        `No bundled or cached binary found; attempting download for v${version}.`,
    );
    try {
        const downloaded = await downloadServerBinary(context, version, output);
        return downloaded;
    } catch (err) {
        const detail = err instanceof Error ? err.message : String(err);
        const platformHint = currentPlatform() ?? `${os.platform()}-${os.arch()}`;
        throw new ServerResolutionError(
            `Could not locate or download hlsl-clippy-lsp for ${platformHint}: ${detail}`,
        );
    }
}
