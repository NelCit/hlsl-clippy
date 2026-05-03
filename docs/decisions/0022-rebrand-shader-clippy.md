---
status: Accepted
date: 2026-05-03
decision-makers: maintainers
consulted: ADR 0006, ADR 0019, ADR 0020, ADR 0021
tags: [release, brand, breaking-change, v2.0]
---

# Rebrand to `shader-clippy` — clean break for v2.0

## Context and Problem Statement

The project shipped under `hlsl-clippy` from Phase 0 through v1.5.6.
The name accurately described the v0–v1.2 surface (HLSL-only AST +
reflection + CFG rules). Two ADRs since then have changed that
surface materially:

- **ADR 0020 (v1.3 sub-phase A)** added `.slang` recognition + the
  reflection-stage rules running end-to-end on Slang sources.
- **ADR 0021 (v1.4 + v1.5 sub-phases B + C)** integrated
  `tree-sitter-slang` so AST + CFG rules cover Slang sources too,
  plus shipped Slang-specific rules (interface conformance,
  associated-type shadowing, etc.).

By v1.5.0 the linter genuinely covered both HLSL and Slang
end-to-end. The `hlsl-clippy` name no longer matched the surface,
and "Slang-compatible HLSL linter" is an unwieldy way to describe
what is now a generic shader linter.

## Decision Drivers

- **Marketing surface accuracy** — the README, blog posts, and
  Marketplace listing all need to lead with "HLSL + Slang" or
  better. A name change is the cleanest way; a tagline change
  alone leaves the slug awkward.
- **Future expansion** — Slang's cross-compile surface gives us
  reflection-stage coverage of MSL/SPIR-V/WGSL targets without
  parsing those languages. A name that doesn't lock us to HLSL
  preserves option value.
- **Brand value of "clippy"** — the linter genre name imported
  from Rust clippy is well-recognised; we want to keep it.
- **API stability commitment (ADR 0019)** — v1.0 promised API
  stability over v1.x, with major-version bumps reserved for
  breaking changes. A binary/CLI/config/extension-ID rename is
  unambiguously a major bump.

## Considered Options

1. **`shader-clippy` (chosen)** — keeps the clippy heritage,
   generalises cleanly across HLSL/Slang/future shader langs, and
   the slug is short.
2. **`slang-clippy`** — narrows the wrong way (we still cover
   HLSL); rejected.
3. **`hlsl-slang-clippy`** — verbose, ugly slug, and would also
   need updating if/when more languages land; rejected.
4. **`gpu-clippy`** — overpromises (implies CUDA / compute /
   non-shader code); rejected.
5. **Keep `hlsl-clippy`, update tagline only** — cheapest, but
   leaves the brand permanently misaligned and undermines the
   v1.4/v1.5 Slang work; rejected as a non-decision.

## Decision

Rename to **`shader-clippy`** across every surface, in a single
v2.0.0 release, with a clean break (no dual-name compat shim).
Specifically:

| Surface | Old | New |
|---|---|---|
| Repo | `NelCit/hlsl-clippy` | `NelCit/shader-clippy` |
| CLI binary | `hlsl-clippy` | `shader-clippy` |
| LSP binary | `hlsl-clippy-lsp` | `shader-clippy-lsp` |
| Config file | `.hlsl-clippy.toml` | `.shader-clippy.toml` |
| Public header dir | `core/include/hlsl_clippy/` | `core/include/shader_clippy/` |
| C++ namespace | `hlsl_clippy::` | `shader_clippy::` |
| CMake targets | `hlsl_clippy_*` | `shader_clippy_*` |
| VS Code extension ID | `nelcit.hlsl-clippy` | `nelcit.shader-clippy` |
| VS Code settings prefix | `hlslClippy.*` | `shaderClippy.*` |
| Suppression syntax | `// hlsl-clippy: allow(…)` | `// shader-clippy: allow(…)` |
| Env vars | `HLSL_CLIPPY_*` | `SHADER_CLIPPY_*` |
| Slang cache root | `%LOCALAPPDATA%/hlsl-clippy/slang/` | `%LOCALAPPDATA%/shader-clippy/slang/` |
| Docs site | `nelcit.github.io/hlsl-clippy/` | `nelcit.github.io/shader-clippy/` |

The `clippy::` diagnostic-code prefix (e.g.
`clippy::language-skip-ast`) stays as-is — it names the linter
genre, not the brand.

## Decision Outcome — clean break, no compat shim

Considered briefly was a one-minor compat window: v2.0 would ship
both names side-by-side (config file fallback, settings-prefix
shim, CLI symlink, env-var alias), with the `hlsl-clippy` aliases
deprecated and removed in v2.1. **Rejected** for two reasons:

1. **Install footprint is small.** v1.0 shipped 2026-04-06; the
   Marketplace listing has weeks of installs, not years. The
   absolute number of users with `.hlsl-clippy.toml` files
   committed to shipping shader trees is bounded.
2. **Compat shims rot.** Carrying two surfaces forward complicates
   every later refactor — every CLI flag parser, every config
   loader, every settings reader has to recognise both prefixes
   and emit the right deprecation warning. The maintenance tax
   compounds; clean break is permanently cheaper after the first
   week of post-merge migration support.

The migration path is one CHANGELOG section; the v1.5.7 farewell
release (a dedicated v1.x branch tag, not a merge to main) ships
to the old `nelcit.hlsl-clippy` Marketplace listing with a README
that points at the new `nelcit.shader-clippy` extension. After
~30 days the old listing is archived.

## Implementation

Sub-phase v2.0a — bulk rename PR (this commit):

- `tools/v2-rebrand.ps1` — one-shot text replacement across
  tracked files, kept in tree as the audit trail.
- 1042 files rewritten across source / docs / fixtures /
  workflows / CMake / CHANGELOG / ADRs / blog posts.
- `core/include/hlsl_clippy/` → `core/include/shader_clippy/`
  (12 public headers).
- Version bump `1.5.6` → `2.0.0` in `core/src/version.cpp` and
  `vscode-extension/package.json`.
- ADR 0022 (this file) added; CLAUDE.md ADR table updated.

Sub-phase v2.0b — repository rename (maintainer action):

- GitHub Settings → Rename repo `hlsl-clippy` → `shader-clippy`.
  GitHub auto-redirects clones, issues, PRs, releases, raw
  content URLs.
- Local clone: `git remote set-url origin
  https://github.com/NelCit/shader-clippy.git`.

Sub-phase v2.0c — Marketplace publication:

- Release workflow publishes `nelcit.shader-clippy` v2.0.0 with
  per-platform `.vsix` bundles.
- Old `nelcit.hlsl-clippy` listing receives a v1.5.7 farewell
  release whose README/CHANGELOG reduces to "moved to
  nelcit.shader-clippy" and reproduces the migration steps from
  the v2.0 CHANGELOG.

Sub-phase v2.0d — local folder rename (maintainer action):

- Close all editors and Claude Code agents.
- `Rename-Item C:\Users\savan\Documents\hlsl-clippy
  shader-clippy` (or platform equivalent).
- Re-launch tooling at the new path.

## Consequences

**Positive:**

- The brand matches the surface again; v1.4/v1.5 Slang work no
  longer needs a paragraph of explanation.
- Future GLSL/MSL/WGSL support (whether via parser additions or
  Slang reflection extension) lands without another rename.
- Bulk rename is mechanical; the rule-pack work and ADR catalog
  are unaffected.

**Negative:**

- Every shipped blog post that references the old name needs
  follow-up cleanup. The bulk script handles in-tree references;
  any external links (HN, Discord, r/GraphicsProgramming
  threads) point at the old repo URL forever. GitHub's redirect
  preserves them.
- Any user with a v1.x install gets a one-time migration cost.
  Mitigated by ADR 0019's API-stability contract — they were
  already preparing for a major bump if it shipped.
- The repo rename + extension-ID change require manual
  maintainer action that I cannot perform from a code-only
  branch.

**Neutral:**

- `tools/v2-rebrand.ps1` lives in the tree forever as the audit
  trail. Not load-bearing; safe to remove in a v2.x cleanup if
  desired.
- The `clippy::` diagnostic-code prefix is brand-neutral and
  stays — there is no v2.1 second rebrand queued.

## Compatibility note

This ADR formally amends ADR 0019's "v1.x maintenance contract"
section. The v1.5 line stays in maintenance for ~30 days
post-v2.0 release at the maintainer's discretion (security fixes
only, not feature work); after that the v1 surface is archived.
