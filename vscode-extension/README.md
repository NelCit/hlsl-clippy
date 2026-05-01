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
- The `hlsl-clippy-lsp` binary on disk. Three options:
  1. **Recommended once GitHub Releases ship the artifact** (sub-phase 5e):
     leave settings empty — the extension will download + cache the binary
     on first activation.
  2. **Build from source** ([`hlsl-clippy` repo](https://github.com/NelCit/hlsl-clippy)).
     The `hlsl-clippy-lsp` binary is built alongside the CLI (`cmake --build
     build-debug --target hlsl-clippy-lsp`); add it to your `PATH` or set
     `hlslClippy.serverPath`.
  3. **Sideload pre-built binary**: drop `hlsl-clippy-lsp` (or
     `hlsl-clippy-lsp.exe` on Windows) somewhere on `PATH`.

> **5c status note:** until sub-phase 5e ships per-platform release
> artifacts, the auto-download path will fail with a 404 and you must use
> option 2 or 3 above.

## Installation

### From the Marketplace (planned for v0.5 launch)

Search for "HLSL Clippy" by `nelcit` in the Extensions view, or install via
`code --install-extension nelcit.hlsl-clippy`.

### From a `.vsix` (sideload, available now)

Download `hlsl-clippy-<version>.vsix` from the
[GitHub Releases](https://github.com/NelCit/hlsl-clippy/releases) page and
install with:

```
code --install-extension hlsl-clippy-<version>.vsix
```

## Settings

| Setting | Type | Default | Purpose |
| --- | --- | --- | --- |
| `hlslClippy.serverPath` | `string` | `""` | Explicit path to the `hlsl-clippy-lsp` binary. Empty = auto-discover (PATH → bundled → cached → download). |
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
