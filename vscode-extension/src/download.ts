// Copyright 2026 NelCit
// SPDX-License-Identifier: Apache-2.0
//
// Download + cache the `hlsl-clippy-lsp` binary from a versioned GitHub
// Release. Per ADR 0014 §6:
//
//   * Asset URL pattern (5c scaffolding — release artifacts land in 5e):
//       https://github.com/NelCit/hlsl-clippy/releases/download/v<version>/
//         hlsl-clippy-lsp-<platform>.<ext>
//     where:
//       <ext>      = "zip" on Windows, "tar.gz" on Linux/macOS
//       <platform> ∈ { windows-x86_64, linux-x86_64, macos-aarch64, macos-x86_64 }
//
//   * Cache directory: `<global_storage>/hlsl-clippy-lsp-<version>/`.
//
//   * Optional SHA-256 verification: if a `<asset>.sha256` file is published
//     alongside the binary, fetch + verify it before extracting.
//
// NOTE (5c): the GitHub release artifacts do not exist yet. This module is
// scaffolded so that 5e can land the workflow without re-touching the
// extension. Until then, the download attempt will fail with HTTP 404.

import { promises as fs } from "fs";
import * as crypto from "crypto";
import * as https from "https";
import * as os from "os";
import * as path from "path";
import * as zlib from "zlib";
import { pipeline } from "stream/promises";
import { createWriteStream, createReadStream } from "fs";
import * as vscode from "vscode";

const k_repoOwner = "NelCit";
const k_repoName = "hlsl-clippy";
const k_binaryBaseName = "hlsl-clippy-lsp";

/**
 * Maps Node's `process.platform` × `process.arch` to the platform string used
 * in release asset filenames. Returns `undefined` for unsupported targets.
 */
export function currentPlatform(): string | undefined {
    const arch = process.arch;
    switch (process.platform) {
        case "win32":
            if (arch === "x64") return "windows-x86_64";
            return undefined;
        case "linux":
            if (arch === "x64") return "linux-x86_64";
            return undefined;
        case "darwin":
            if (arch === "arm64") return "macos-aarch64";
            if (arch === "x64") return "macos-x86_64";
            return undefined;
        default:
            return undefined;
    }
}

function archiveExtension(): string {
    return process.platform === "win32" ? "zip" : "tar.gz";
}

function binaryName(): string {
    return process.platform === "win32" ? `${k_binaryBaseName}.exe` : k_binaryBaseName;
}

/**
 * Build the canonical release asset URL.
 *   https://github.com/<owner>/<repo>/releases/download/v<version>/
 *     hlsl-clippy-lsp-<platform>.<ext>
 */
export function buildAssetUrl(version: string, platform: string): string {
    const ext = archiveExtension();
    return (
        `https://github.com/${k_repoOwner}/${k_repoName}/releases/download/` +
        `v${version}/${k_binaryBaseName}-${platform}.${ext}`
    );
}

export function buildChecksumUrl(version: string, platform: string): string {
    return `${buildAssetUrl(version, platform)}.sha256`;
}

interface HttpResponse {
    readonly status: number;
    readonly headers: Record<string, string | string[] | undefined>;
    readonly body: Buffer;
}

function httpGet(url: string, redirectsLeft = 5): Promise<HttpResponse> {
    return new Promise((resolve, reject) => {
        const request = https.get(url, (res) => {
            const status = res.statusCode ?? 0;
            // Follow redirects (GitHub release downloads redirect to a CDN).
            if (
                status >= 300 &&
                status < 400 &&
                typeof res.headers.location === "string" &&
                redirectsLeft > 0
            ) {
                res.resume();
                httpGet(res.headers.location, redirectsLeft - 1)
                    .then(resolve)
                    .catch(reject);
                return;
            }
            const chunks: Buffer[] = [];
            res.on("data", (chunk: Buffer) => chunks.push(chunk));
            res.on("end", () => {
                resolve({
                    status,
                    headers: res.headers,
                    body: Buffer.concat(chunks),
                });
            });
            res.on("error", reject);
        });
        request.on("error", reject);
        request.setTimeout(60_000, () => {
            request.destroy(new Error("HTTP request timed out after 60s"));
        });
    });
}

async function sha256OfFile(filepath: string): Promise<string> {
    const hash = crypto.createHash("sha256");
    await pipeline(createReadStream(filepath), hash);
    return hash.digest("hex");
}

async function ensureDir(dir: string): Promise<void> {
    await fs.mkdir(dir, { recursive: true });
}

async function extractArchive(
    archivePath: string,
    destDir: string,
): Promise<string> {
    await ensureDir(destDir);
    if (archivePath.endsWith(".zip")) {
        return await extractZip(archivePath, destDir);
    }
    if (archivePath.endsWith(".tar.gz") || archivePath.endsWith(".tgz")) {
        return await extractTarGz(archivePath, destDir);
    }
    throw new Error(`Unsupported archive format: ${archivePath}`);
}

/**
 * Minimal tar.gz extractor that pulls out the single binary entry. We do not
 * pull in `tar` / `unzipper` to keep the extension's `node_modules` slim;
 * the archive layout is under our control (5e workflow ships exactly one
 * binary per archive, optionally with a top-level directory).
 */
async function extractTarGz(archivePath: string, destDir: string): Promise<string> {
    const gz = await fs.readFile(archivePath);
    const tarBuf: Buffer = await new Promise((resolve, reject) => {
        zlib.gunzip(gz, (err, buf) => (err ? reject(err) : resolve(buf)));
    });

    const exe = binaryName();
    let offset = 0;
    while (offset + 512 <= tarBuf.length) {
        const header = tarBuf.subarray(offset, offset + 512);
        offset += 512;

        // End-of-archive: two consecutive zero blocks.
        if (header.every((b) => b === 0)) {
            break;
        }

        const rawName = header.subarray(0, 100).toString("utf8").replace(/\0+$/, "");
        const rawSize = header.subarray(124, 136).toString("ascii").replace(/[\0 ]+$/, "");
        const size = parseInt(rawSize, 8);
        const typeflag = String.fromCharCode(header[156]!);

        const dataEnd = offset + size;
        const baseName = path.basename(rawName);
        if (typeflag === "0" || typeflag === "" || typeflag === "\0") {
            if (baseName === exe) {
                const outPath = path.join(destDir, exe);
                await fs.writeFile(outPath, tarBuf.subarray(offset, dataEnd));
                if (process.platform !== "win32") {
                    await fs.chmod(outPath, 0o755);
                }
                return outPath;
            }
        }
        // Round up to the next 512-byte boundary.
        offset = dataEnd + ((512 - (size % 512)) % 512);
    }
    throw new Error(`Archive ${archivePath} does not contain ${exe}`);
}

/**
 * Minimal zip extractor that scans the central directory and inflates the
 * single binary entry. Supports STORE (0) and DEFLATE (8) only — sufficient
 * for the canonical zip layout the release workflow produces.
 */
async function extractZip(archivePath: string, destDir: string): Promise<string> {
    const buf = await fs.readFile(archivePath);
    const exe = binaryName();

    // Locate End-of-Central-Directory (EOCD) record by scanning backwards.
    let eocd = -1;
    for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) {
        if (buf.readUInt32LE(i) === 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd === -1) {
        throw new Error(`Zip ${archivePath}: EOCD not found`);
    }
    const cdEntries = buf.readUInt16LE(eocd + 10);
    const cdOffset = buf.readUInt32LE(eocd + 16);

    let cursor = cdOffset;
    for (let i = 0; i < cdEntries; i++) {
        if (buf.readUInt32LE(cursor) !== 0x02014b50) {
            throw new Error(`Zip ${archivePath}: bad central directory header`);
        }
        const compMethod = buf.readUInt16LE(cursor + 10);
        const compSize = buf.readUInt32LE(cursor + 20);
        const nameLen = buf.readUInt16LE(cursor + 28);
        const extraLen = buf.readUInt16LE(cursor + 30);
        const commentLen = buf.readUInt16LE(cursor + 32);
        const localHdrOffset = buf.readUInt32LE(cursor + 42);
        const name = buf
            .subarray(cursor + 46, cursor + 46 + nameLen)
            .toString("utf8");

        if (path.basename(name) === exe) {
            // Read local file header to compute data offset.
            if (buf.readUInt32LE(localHdrOffset) !== 0x04034b50) {
                throw new Error(
                    `Zip ${archivePath}: bad local header for ${name}`,
                );
            }
            const localNameLen = buf.readUInt16LE(localHdrOffset + 26);
            const localExtraLen = buf.readUInt16LE(localHdrOffset + 28);
            const dataStart = localHdrOffset + 30 + localNameLen + localExtraLen;
            const compressed = buf.subarray(dataStart, dataStart + compSize);

            let payload: Buffer;
            if (compMethod === 0) {
                payload = compressed;
            } else if (compMethod === 8) {
                payload = zlib.inflateRawSync(compressed);
            } else {
                throw new Error(
                    `Zip ${archivePath}: unsupported compression method ${compMethod}`,
                );
            }

            const outPath = path.join(destDir, exe);
            await fs.writeFile(outPath, payload);
            return outPath;
        }
        cursor += 46 + nameLen + extraLen + commentLen;
    }
    throw new Error(`Zip ${archivePath} does not contain ${exe}`);
}

/**
 * Download the LSP server binary archive for the given version, verify its
 * SHA-256 if a checksum file is published, extract it into the global-storage
 * cache, and return the path to the extracted binary.
 *
 * Uses a `vscode.window.withProgress` notification so the user sees what is
 * happening on first activation (per ADR 0014 §6).
 */
export async function downloadServerBinary(
    context: vscode.ExtensionContext,
    version: string,
    output: vscode.OutputChannel | undefined,
): Promise<string> {
    const platform = currentPlatform();
    if (platform === undefined) {
        throw new Error(
            `Unsupported platform/arch: ${process.platform}/${process.arch}. ` +
                `Set hlslClippy.serverPath manually.`,
        );
    }

    const url = buildAssetUrl(version, platform);
    const checksumUrl = buildChecksumUrl(version, platform);
    const cacheDir = path.join(
        context.globalStorageUri.fsPath,
        `${k_binaryBaseName}-${version}`,
    );
    await ensureDir(cacheDir);

    const tmpName = `download-${Date.now()}.${archiveExtension()}`;
    const tmpPath = path.join(os.tmpdir(), tmpName);

    output?.appendLine(`[hlsl-clippy] Downloading ${url}`);

    return await vscode.window.withProgress(
        {
            location: vscode.ProgressLocation.Notification,
            title: `Downloading hlsl-clippy-lsp v${version}`,
            cancellable: false,
        },
        async (progress) => {
            progress.report({ message: "Fetching archive..." });
            const archive = await httpGet(url);
            if (archive.status !== 200) {
                throw new Error(
                    `HTTP ${archive.status} fetching ${url}. ` +
                        `Release artifact may not be published yet ` +
                        `(see ADR 0014, sub-phase 5e).`,
                );
            }
            await pipeline(
                (async function* () {
                    yield archive.body;
                })(),
                createWriteStream(tmpPath),
            );

            // Optional checksum verification. Missing checksum file is a
            // soft failure for v0.5; log and continue.
            try {
                progress.report({ message: "Verifying checksum..." });
                const checksumResp = await httpGet(checksumUrl);
                if (checksumResp.status === 200) {
                    const expected = checksumResp.body
                        .toString("utf8")
                        .split(/\s+/)[0]!
                        .trim()
                        .toLowerCase();
                    const actual = (await sha256OfFile(tmpPath)).toLowerCase();
                    if (expected !== actual) {
                        throw new Error(
                            `Checksum mismatch: expected ${expected}, got ${actual}`,
                        );
                    }
                    output?.appendLine(`[hlsl-clippy] Checksum verified.`);
                } else {
                    output?.appendLine(
                        `[hlsl-clippy] No checksum file (HTTP ${checksumResp.status}); skipping verification.`,
                    );
                }
            } catch (err) {
                if (err instanceof Error && err.message.startsWith("Checksum mismatch")) {
                    throw err;
                }
                output?.appendLine(
                    `[hlsl-clippy] Checksum fetch failed: ${
                        err instanceof Error ? err.message : String(err)
                    } (continuing without verification).`,
                );
            }

            progress.report({ message: "Extracting..." });
            const extracted = await extractArchive(tmpPath, cacheDir);
            await fs.unlink(tmpPath).catch(() => {
                /* best-effort cleanup */
            });
            output?.appendLine(`[hlsl-clippy] Server binary extracted to ${extracted}`);
            return extracted;
        },
    );
}
