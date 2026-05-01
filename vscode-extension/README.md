# HLSL Clippy — VS Code extension

[![Apache 2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](https://github.com/NelCit/hlsl-clippy/blob/main/LICENSE)

In-editor diagnostics, hover docs, and quick-fix code actions for HLSL —
powered by the [`hlsl-clippy`](https://github.com/NelCit/hlsl-clippy) linter.
This extension is a thin wrapper around the `hlsl-clippy-lsp` JSON-RPC
server (see ADR 0014).

## Features

- Inline diagnostics as you type, with the same rule ids the CI gate emits.
- Code-action quick-fixes for every `machine_applicable` rule (lightbulb menu).
- Hover tooltips that link to the per-rule documentation page.
- Workspace-aware: respects `.hlsl-clippy.toml` walk-up resolution and
  multi-root projects (per ADR 0014 §4).
- No telemetry. The extension never reports back. Diagnostics stay local.

The diagnostics surfaced in VS Code's Problems panel are byte-for-byte the
same as the diagnostics the CLI emits in CI logs (modulo formatting).

## Requirements

- VS Code 1.85 or newer.

The `hlsl-clippy-lsp` server binary is **bundled inside the extension** —
since v0.5.3 the Marketplace ships a per-platform `.vsix` for each of
`linux-x64`, `win32-x64`, and `darwin-arm64`, and the matching binary
lives at `<extension>/server/<platform>/hlsl-clippy-lsp[.exe]`. Marketplace
installs and `.vsix` sideloads both Just Work with no extra download.

Power-user overrides (only set these if you need them):

- **`hlslClippy.serverPath`** — point at a custom-built `hlsl-clippy-lsp`
  binary (e.g. a local debug build). Bypasses the bundled binary.
- **PATH lookup** — if `hlsl-clippy-lsp` (or `hlsl-clippy-lsp.exe`) is on
  `PATH`, it takes precedence over the bundled binary.

## Installation

### From the Marketplace

Search for **"HLSL Clippy"** by `nelcit` in the Extensions view (`Ctrl+Shift+X`),
or install from the command line:

```
code --install-extension nelcit.hlsl-clippy
```

The Marketplace serves the matching `.vsix` for your OS+arch automatically;
the LSP binary is included.

### From a `.vsix` (sideload)

Per-platform `.vsix` files are attached to every
[GitHub Release](https://github.com/NelCit/hlsl-clippy/releases) — pick the
one for your platform:

- `hlsl-clippy-<version>-linux-x64.vsix`
- `hlsl-clippy-<version>-win32-x64.vsix`
- `hlsl-clippy-<version>-darwin-arm64.vsix`

Then:

```
code --install-extension hlsl-clippy-<version>-<target>.vsix
```

## Settings

| Setting | Type | Default | Purpose |
| --- | --- | --- | --- |
| `hlslClippy.serverPath` | `string` | `""` | Explicit path to a custom `hlsl-clippy-lsp` binary. Empty = use the binary bundled with the extension (or `hlsl-clippy-lsp` on `PATH`). |
| `hlslClippy.targetProfile` | `string` | `""` | Slang target profile (e.g. `sm_6_6`, `vs_6_7`, `ps_6_8`). Empty = server default per stage. Forwarded to `LintOptions::target_profile`. |
| `hlslClippy.enableReflection` | `boolean` | `true` | Enable Phase 3 reflection-aware rules. Disable on slow machines to keep AST-only latency. |
| `hlslClippy.enableControlFlow` | `boolean` | `true` | Enable Phase 4 CFG-aware rules. |
| `hlslClippy.trace.server` | `string` | `"off"` | Trace LSP communication (`off` / `messages` / `verbose`). |

## Commands

| Command | Description |
| --- | --- |
| `HLSL Clippy: Restart Server` | Stop and re-spawn the LSP server (useful after a binary update). |
| `HLSL Clippy: Show Output Channel` | Reveal the extension's output panel. |

## Rule documentation

Every diagnostic includes a hover link to the rule's docs page. Browse the
full catalog at
[`docs/rules/`](https://github.com/NelCit/hlsl-clippy/tree/main/docs/rules).

## License

Apache-2.0. See [`LICENSE`](https://github.com/NelCit/hlsl-clippy/blob/main/LICENSE)
in the repo root.

This extension bundles `vscode-languageclient` (MIT, Microsoft Corp.) and
`@types/vscode` (MIT). Third-party license texts are reproduced in
[`THIRD_PARTY_LICENSES.md`](https://github.com/NelCit/hlsl-clippy/blob/main/THIRD_PARTY_LICENSES.md).

## Reporting issues

File bugs at
[https://github.com/NelCit/hlsl-clippy/issues](https://github.com/NelCit/hlsl-clippy/issues).
Please include:

- The output of `HLSL Clippy: Show Output Channel`.
- Your `hlslClippy.*` settings.
- A minimal reproducer shader.
