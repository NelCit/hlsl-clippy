---
title: What's new
outline: deep
---

# What's new

Format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/);
canonical source is [`CHANGELOG.md`](https://github.com/NelCit/shader-clippy/blob/main/CHANGELOG.md)
in the repo root. Per-release artifacts live on the
[GitHub Releases](https://github.com/NelCit/shader-clippy/releases) page.

## 0.5.0 — 2026-05-01

Initial public release. **154 rules** across math, bindings, texture,
workgroup, control-flow, mesh, DXR, work-graphs, SER, cooperative-vector,
long-vectors, opacity-micromaps, sampler-feedback, VRS, and
wave-helper-lane. Phases 0 → 5 of the [roadmap](/roadmap) shipped
end-to-end.

### Highlights

- **CLI rule engine + machine-applicable `--fix`** — rustc-style
  diagnostics; per-line / per-block / per-file `// shader-clippy: allow(...)`
  inline suppressions; idempotent quick-fix rewriting.
- **`.shader-clippy.toml` config** — per-rule severity, includes /
  excludes globs, per-directory `[[overrides]]`. Walk-up resolution
  bounded by the first `.git/` ancestor.
- **Reflection-aware rule packs** (Phase 3, [ADR 0012](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0012-phase-3-reflection-infrastructure.md))
  — Slang reflection plumbed into `RuleContext` for buffer / sampler /
  root-signature / cbuffer-layout / SM 6.7-6.9 surface rules.
- **Control-flow + uniformity rules** (Phase 4, [ADR 0013](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0013-phase-4-control-flow-infrastructure.md))
  — CFG over the tree-sitter AST with a Lengauer-Tarjan dominator
  tree, taint-propagation uniformity oracle, helper-lane analyzer.
- **LSP server + VS Code extension** (Phase 5, [ADR 0014](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0014-phase-5-lsp-architecture.md))
  — `shader-clippy-lsp` JSON-RPC server thinly wrapping `core`;
  TypeScript extension under the `nelcit` publisher; quick-fixes
  surfaced as VS Code code actions.
- **CI gate-mode polish** ([ADR 0015](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0015-phase-6-launch-plan.md))
  — `--format=json` for stable CI consumption; `--format=github-annotations`
  for inline PR diff annotations on GitHub Actions; auto-detection
  of `$GITHUB_ACTIONS=true`. Drop the [example workflow](https://github.com/NelCit/shader-clippy/blob/main/docs/ci/lint-hlsl-example.yml)
  into your own repo to wire it up.
- **Cross-platform release pipeline** — tag-driven builds on
  `windows-x86_64`, `linux-x86_64`, and `macos-aarch64`, with
  graceful skip on absent secrets for Windows code signing and
  macOS notarization.
- **Hosted docs site** at <https://nelcit.github.io/shader-clippy/>.

### Known limitations (deferred)

- `cbuffer X : register(b0)` parses to an ERROR node in
  tree-sitter-hlsl; rules that need the binding fall back to Slang
  reflection. See [external/treesitter-version.md](https://github.com/NelCit/shader-clippy/blob/main/external/treesitter-version.md).
- Mesh-node rules from SM 6.9 preview are config-gated behind
  `[experimental] work-graph-mesh-nodes = true` until the Agility
  SDK preview API stabilises.
- Four golden-snapshot integration tests are flaky on
  `STATUS_STACK_BUFFER_OVERRUN` and disabled by default; tracked for
  v0.5.1.
- VS Code Marketplace listing is gated on the `VSCE_PAT` repo
  secret; the .vsix is attached to every GitHub Release for sideload
  via `code --install-extension shader-clippy-0.5.0.vsix` regardless.

## Quick links

- [Full changelog (main)](https://github.com/NelCit/shader-clippy/blob/main/CHANGELOG.md)
- [Releases page](https://github.com/NelCit/shader-clippy/releases)
- [Phase status](/roadmap)
