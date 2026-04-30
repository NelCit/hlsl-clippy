# LSP Integration

> **Status:** pre-v0 — the LSP server lands in Phase 5. See [ROADMAP](../ROADMAP.md).

## Planned features

The LSP server reuses the same diagnostic and fix engine as the CLI. The JSON-RPC layer is thin; there is no separate analysis pass for the editor path.

Planned capabilities when Phase 5 ships:

- **VS Code extension** — thin wrapper that starts the LSP server as a child process and proxies JSON-RPC.
- **Generic LSP** — any editor with LSP support (Neovim, Helix, Emacs, Zed, etc.) can use the server without the VS Code extension.
- **Workspace-aware config** — the server walks the workspace root to find `.hlsl-clippy.toml` and respects per-directory `[[overrides]]` sections.
- **Code-action quick-fixes** — machine-applicable fixes (see [rules catalog](rules/index.md)) surface as code actions in the editor UI. Suggestion-level fixes are shown but require explicit acceptance.
- **Diagnostics on save** — re-lint is triggered on `textDocument/didSave`. Incremental tree-sitter re-parse keeps latency low for typical edit sizes.
- **`#include` resolution** — Slang's include resolution is wired through LSP workspace folders and `slang.includePaths` config so cross-file rules see the full compiled unit.

## Configuration

No LSP-specific configuration is planned. The server reads `.hlsl-clippy.toml` using the same resolution logic as the CLI.

## Editor setup (placeholder)

Editor-specific setup instructions will be added once Phase 5 ships.
