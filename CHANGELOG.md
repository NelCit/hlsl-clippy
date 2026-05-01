# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Phase 5 — LSP server (`hlsl-clippy-lsp`) thinly wrapping `core` over
  JSON-RPC, plus a TypeScript VS Code extension (`vscode-extension/`,
  publisher `nelcit`) that activates on the `hlsl` language id and
  surfaces diagnostics + quick-fix code actions.
- macOS CI runner (`macos-14`, Apple Silicon) wired into the build matrix.
- Phase 3 reflection-aware rule packs (ADR 0007 Phase 3, ADR 0010 SM 6.9)
  gated on `LintOptions::enable_reflection`.
- Release-artifact pipeline (`.github/workflows/release.yml`): tag-triggered
  builds for `windows-x86_64`, `linux-x86_64`, and `macos-aarch64`; bundles
  the CLI + LSP binaries with LICENSE / NOTICE / THIRD_PARTY_LICENSES.md;
  publishes archives + SHA-256 sums to the GitHub Release. Optional macOS
  notarization (gated on `APPLE_NOTARY_KEY`) and Windows code signing
  (gated on `WINDOWS_CERT`) — both no-op gracefully when secrets are
  absent.
- VS Code Marketplace publish workflow
  (`.github/workflows/release-vscode.yml`): tag-triggered build of the
  `.vsix`, conditional `vsce publish` gated on the `VSCE_PAT` secret, and
  a `.vsix` asset upload to the GitHub Release for users who sideload.
- Pre-tag release checklist at `tools/release-checklist.md`.
- Directory layout: `cli/` (CLI binary) and `core/` (library), replacing the
  earlier `crates/` placeholder.
- Modular CMake build: `hlsl_clippy_core` static library target and
  `hlsl-clippy` CLI executable target.
- Test corpus under `tests/corpus/`: 17 permissively-licensed HLSL shaders.
- Test fixtures under `tests/fixtures/`: expected diagnostics for Phase 2, 3,
  and 4 rule validation.
- GitHub Actions CI workflows: build matrix (Windows/Linux), lint gate, and
  CodeQL stub.
- Slang vendored as a git submodule with a CMake smoke test.
- tree-sitter and tree-sitter-hlsl vendored with a CMake smoke test.
- Documentation scaffolding: `docs/` tree, governance files
  (`CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`), and GitHub issue/PR
  templates.
- Inline suppression parser (`// hlsl-clippy: allow(rule-name)`) with line,
  block, and file scopes.
- Declarative TSQuery wrapper for AST-pattern rules.
- `redundant-saturate` rule with machine-applicable fix.
- `clamp01-to-saturate` rule with machine-applicable fix.
- Quick-fix `Rewriter` framework + `--fix` CLI flag (idempotent application).
- `.hlsl-clippy.toml` config loader (toml++) with rule severity,
  includes/excludes, per-directory overrides; `--config <path>` CLI flag.

### Changed

- Switched from MIT to Apache-2.0 license; added NOTICE and
  THIRD_PARTY_LICENSES.md.

### Fixed

- _(none this cycle)_

### Deprecated

- _(none this cycle)_
