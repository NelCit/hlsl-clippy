# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.5.2] — 2026-05-01

Same-day continuation. v0.5.1's CI fixes worked: the Slang prebuilt
fetch resolved cleanly on Linux + macOS, libc++ + unversioned-clang
landed correctly, and the build progressed to step 72/187 on macOS
(versus 0/666 on v0.5.0). One Clang-strict warning surfaced and
killed it — fix in this release.

### Fixed

- `core/src/rules/dead_store_sv_target.cpp`: removed an unused
  `trim()` static helper. MSVC `/W4` doesn't flag unused-static
  functions but Clang `-Wunused-function` + `-Werror` does, so
  Linux + macOS hard-failed at step 72 of the binary release. The
  function had no callers in the file; deletion is a no-op for
  rule behavior.

## [0.5.1] — 2026-05-01

Same-day post-launch hardening. v0.5.0 shipped the .vsix Marketplace
artifact correctly but the binary `Release` workflow failed on Linux
+ macOS at the from-source Slang build step. v0.5.1 repairs the
release pipeline and ships the CLI/LSP archives that v0.5.0 missed.

### Changed

- **Slang now resolves via a per-user prebuilt cache, not a from-source
  submodule build.** The `external/slang` git submodule was retired;
  `cmake/UseSlang.cmake` resolves Slang via `Slang_ROOT` (escape
  hatch) → `~/.cache/hlsl-clippy/slang/<version>/` (the cache
  populated by `tools/fetch-slang.{sh,ps1}`). CI runs that previously
  spent ~20 minutes compiling Slang now spend ~10 seconds downloading
  the matching prebuilt tarball. `git clone` is meaningfully smaller
  too.

### Fixed

- `release.yml` Linux: now installs `libc++-18-dev` and sets
  `CXXFLAGS=-stdlib=libc++` / `LDFLAGS=-stdlib=libc++`. Without these
  the build hit "no template named 'expected' in namespace 'std'"
  under Ubuntu 24.04's libstdc++ 13. (`ci.yml` already had this fix;
  `release.yml` had drifted.)
- `release.yml` macOS: switched to unversioned `clang` / `clang++`
  invocations (Homebrew's `llvm@18` is keg-only and ships unversioned
  binaries — the previous `clang-18` / `clang++-18` calls failed with
  "No such file or directory").
- `release-vscode.yml`: skip-on-prerelease-tag gate added so future
  `-rc1` / `-beta` tag tests do not hard-fail on the VS Marketplace's
  rejection of SemVer prerelease suffixes.
- `softprops/action-gh-release@v2` pinned to commit SHA in both
  release workflows (the `@v2` tag-pin was flagged as an
  orchestrator follow-up in the workflow headers).

### Added

- **Phase 6 launch blog series** at <https://nelcit.github.io/hlsl-clippy/blog/>:
  the `Why your HLSL is slower than it has to be` preface plus eight
  category overviews (math, workgroup, control-flow, bindings,
  texture, mesh+DXR, wave+helper-lane, SM 6.9 / SER+coop-vec). Each
  overview deep-dives the GPU mechanism behind that rule pack and
  links to the per-rule pages.
- `--format=json` and `--format=github-annotations` flags on the
  CLI (sub-phase 6a). The latter auto-selects when `$GITHUB_ACTIONS=true`.
  `docs/ci/lint-hlsl-example.yml` is a copy-paste-able starter
  workflow.
- `package-lock.json` at repo root for reproducible `npm ci` in
  `docs.yml` + `release-vscode.yml`.

### Documentation

- 2026-05-01 audit-driven sweep of `ROADMAP.md` + `CLAUDE.md`: 92
  shipped rules previously listed as `- [ ]` are now `- [x]`;
  CLAUDE.md "current status" block rewritten to match the actual
  Phase 0 → 5 done state instead of the stale "Phase 2 queued"
  text. ADR count corrected from 10 to 15.

## [0.5.0] — 2026-05-01

Initial public release. **154 rules** ship across math, bindings, texture,
workgroup, control-flow, mesh, DXR, work-graphs, SER, cooperative-vector,
long-vectors, opacity-micromaps, sampler-feedback, VRS, and
wave-helper-lane. Phases 0 → 5 of the roadmap are complete; Phase 6
(launch) is in progress around this release.

### Added

- Phase 5 — LSP server (`hlsl-clippy-lsp`) thinly wrapping `core` over
  JSON-RPC, plus a TypeScript VS Code extension (`vscode-extension/`,
  publisher `nelcit`) that activates on the `hlsl` language id and
  surfaces diagnostics + quick-fix code actions.
- macOS CI runner (`macos-14`, Apple Silicon) wired into the build matrix.
- Phase 4 — control-flow / data-flow infrastructure (ADR 0013): CFG
  built over the tree-sitter AST with a Lengauer-Tarjan dominator tree, a
  taint-propagation uniformity oracle, helper-lane analyzer, and bounded
  inter-procedural inlining (`cfg_inlining_depth = 3`). Plus the rule
  packs that ride on it: control-flow / divergence / atomics /
  helper-lane (e.g. `derivative-in-divergent-cf`,
  `barrier-in-divergent-cf`, `wave-intrinsic-non-uniform`,
  `branch-on-uniform-missing-attribute`, `small-loop-no-unroll`,
  `loop-invariant-sample`, `groupshared-stride-non-32-bank-conflict`,
  `groupshared-atomic-replaceable-by-wave`, `dispatchmesh-not-called`).
- Phase 3 — reflection-aware rule packs (ADR 0007 Phase 3, ADR 0010
  SM 6.9) gated on `LintOptions::enable_reflection`. Sub-phases:
  3a (reflection infra per ADR 0012 — opaque `reflection.hpp`,
  `Stage::Reflection`, `Rule::on_reflection`, lazy
  per-`(SourceId, target-profile)` cached `ReflectionEngine`),
  3b (shared utilities), 3c (5 parallel rule packs covering buffers,
  groupshared-typed, samplers, root-sig, compute, wave, state, plus the
  ADR 0010 SM 6.9 surfaces — SER, Cooperative Vectors, Long Vectors,
  OMM, Mesh Nodes preview-gated).
- Phase 2 — AST-only rule pack (ADR 0009): math / saturate-redundancy /
  misc category packs adding 24 net-new rules.
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

[0.5.2]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/NelCit/hlsl-clippy/releases/tag/v0.5.0
