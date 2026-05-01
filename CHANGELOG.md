# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [0.5.6] — 2026-05-01

Same-day continuation. v0.5.5 binary Release failed on Windows due to a
`$Triple` reference in `tools/fetch-slang.ps1` (script is Windows-only,
no `$Triple` parameter — leftover from a copy-paste of the bash variant).
This release fixes that plus three more CI workflow regressions surfaced
during the audit-driven cleanup chain.

### Fixed

- `tools/fetch-slang.ps1` — replace `$Triple.ToUpper()` (variable doesn't
  exist on the Windows-only script) with the hardcoded
  `WINDOWS_X86_64` triple in the env-var name. Linux/macOS bash variant
  was unaffected.
- `.github/workflows/lint.yml` — add `tools/fetch-slang.sh` step before
  `cmake configure`. Since commit 73c0322 retired the from-source
  submodule fallback, lint's compile_commands.json generation hit a
  hard FATAL_ERROR on every push. Also installs `libc++-18-dev` /
  `libc++abi-18-dev` and sets `CXXFLAGS=-stdlib=libc++` to match the
  ci.yml + release.yml toolchain shape.
- `tests/CMakeLists.txt` — mark Catch2's interface include directories
  as SYSTEM via `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` so
  `hlsl_clippy_warnings`'s `-Werror -Wnon-virtual-dtor` doesn't
  trigger on Catch2's `BinaryExpr<>` template internals. macOS Clang 18
  surfaces this warning where Linux Clang 18 doesn't (different libc++
  version); SYSTEM-include attribution suppresses it consistently.

## [0.5.5] — 2026-05-01

Same-day continuation. v0.5.4 binary Release failed on the
windows-x86_64 leg with a PowerShell syntax error in the new SHA-256
verification block I'd added in v0.5.4 (`$env:$VarName` doesn't expand
the way bash's `${!VAR}` does). v0.5.5 fixes that and ships the
binaries that v0.5.4 couldn't.

### Fixed

- `tools/fetch-slang.ps1` — replaced `$env:$TripleVarName` (PowerShell
  syntax error) with `[System.Environment]::GetEnvironmentVariable(...)`.
  Linux/macOS were unaffected (bash uses `${!VAR}` indirect expansion
  which works correctly).

### Added

- **Multi-file CLI invocation.** `hlsl-clippy lint a.hlsl b.hlsl c.hlsl`
  now lints all three files in one process, amortizing Slang
  `IGlobalSession` + ReflectionEngine cache + CFG engine cache + the
  154-rule registry across the whole tree. Previously each file
  required a separate process — the runtime-perf audit flagged this
  as the single highest-ROI perf win (3-10× speedup on tree-wide CI
  gates). JSON output remains a single combined array across all
  files; human format gets a per-file count summary at the end.
- **`docs/troubleshooting.md`** — first FAQ page covering the most
  common install / build / lint / VS Code / CI failure modes flagged
  by the user-facing-docs audit. Wired into the docs sidebar.
- **`tests/KNOWN_FAILURES.md`** — documents the 4 pre-existing
  golden-snapshot crashes so new contributors aren't alarmed by a
  fresh `ctest` reporting 4 failures.
- **`CMakePresets.json`** — `dev-debug` / `dev-release` / `ci-clang` /
  `ci-msvc` configure presets matching the names CLAUDE.md and
  CONTRIBUTING.md reference. Previously the documented preset
  commands all returned `No such preset`.
- **`tools/dev-shell.sh`** — POSIX equivalent of `dev-shell.ps1`.
  Detects OS, validates clang-18 / brew llvm@18, prepends keg bin/
  to PATH on macOS, runs `tools/fetch-slang.sh` if the cache is
  empty, exports `Slang_ROOT`. Idempotent via
  `HLSL_CLIPPY_DEV_SHELL_READY` guard.
- **`.github/dependabot.yml`** — weekly bumps for github-actions,
  npm (root + vscode-extension), and git submodules. Slang +
  tomlplusplus excluded (manual SHA-rotation maintainer tasks).
- **OpenGraph + sitemap** — `docs/.vitepress/config.mts` now ships
  Twitter card meta, OpenGraph site description, and a sitemap with
  the GitHub Pages hostname. Improves HN/Reddit/Twitter preview
  rendering of shared docs links.
- **Reflection multi-call regression test** in `test_reflection.cpp`
  — locks in commit `36e7cd4` (Slang module-name uniquification).
  Tagged `[regression]`.

### Changed

- `docs.yml` `actions/setup-node` SHA pin unified to
  `0a44ba7841725637a19e28fa30b79a866c81b0a6` (matches
  release-vscode.yml; both files claimed v4.0.4 with divergent
  SHAs). `cache: 'npm'` re-enabled now that `package-lock.json`
  ships at repo root.
- 4 rule doc pages (`gather-channel-narrowing`,
  `min16float-opportunity`, `texture-array-known-slice-uniform`,
  `texture-as-buffer`) — `severity: info` → `severity: note` (info
  was outside the loader's allow list).
- `core/src/source.cpp` clang-format compliance fix (the
  `max_file_bytes()` helper added in v0.5.4 had one over-long line
  that broke the Lint workflow on every commit since 08a4640).

## [0.5.4] — 2026-05-01

Audit-driven cleanup pass. The 2026-05-01 multi-domain audit chain
(11 parallel agents covering legal, VS Code UX, CI/CD, C++ arch+build
perf, runtime perf, misc, security, error handling, onboarding,
user-facing docs, rule docs+tests) flagged a cluster of pre-launch
blockers; this release closes the 18 highest-severity items.

### Changed

- **Documentation truth pass.** Every narrative `docs/*.md` page
  (`getting-started.md`, `configuration.md`, `ci.md`, `lsp.md`)
  rewritten against v0.5.x reality — the stale `> Status: pre-v0`
  banners are gone, install instructions are real, severity vocabulary
  in the configuration reference matches the loader (`error|warning|note|off`),
  CI page documents the shipped `--format=github-annotations` flag (was
  `--format=github`), LSP page documents Marketplace install + per-platform
  `.vsix` bundling + Neovim/Helix/Emacs recipes.
- **README install section** rewritten — adds prebuilt-from-Releases as
  the primary path, `tools/fetch-slang.{sh,ps1}` bootstrap step
  (without it, the previous quickstart `cmake -B build` failed at
  configure time per the onboarding audit), per-platform first-time
  toolchain install hints (apt llvm.sh + libc++-18-dev, brew llvm@18 +
  PATH note for macOS, VS 2022 17.14+).
- **CLAUDE.md** "Current status" + "What this project is" + "Locked
  technical decisions" blocks resynced to v0.5.3 reality. Inline
  `**Proposed**` markers flipped to `**Accepted**` for the ADRs that
  shipped (0008/0010/0011/0012/0013/0014/0015). ADR 0003
  (apps/libs/include layout) alone stays Proposed — the architecture
  audit found "no concrete harm" of staying with the current cli/core/
  lsp/ split.
- **Code-action title in LSP** dropped the redundant "Apply quick-fix:"
  prefix. VS Code already groups code actions by `kind: quickfix`;
  the title now reads as a sentence (e.g. "Replace pow(x, 2.0) with
  x * x" instead of "Apply quick-fix: Replace pow(x, 2.0) with x * x").
- **VS Code Marketplace metadata** — `package.json` gains a
  `galleryBanner` (`#1e1e1e` dark theme) and an additional category
  (`"Programming Languages"` alongside `"Linters"`); keyword list
  expanded (`shader-lint`, `d3d12`, `performance`, `clippy` added);
  redundant `activationEvents: ["onLanguage:hlsl"]` removed (VS Code
  ≥ 1.74 auto-activates on any language declared in
  `contributes.languages`, and our `engines.vscode` floor is `^1.85.0`).
- **GSL claim resolution.** ADR 0006 + CLAUDE.md + ROADMAP.md
  referenced Microsoft GSL as a project code standard, but the legal
  audit caught that it was never actually linked into the build (no
  `<gsl/...>` includes anywhere). Replaced the GSL bullet with the
  C++23 stdlib equivalents we actually use (`std::span`, references
  / asserted bare pointers, `static_cast` with explicit asserts);
  `THIRD_PARTY_LICENSES.md` no longer needs a GSL section.

### Security

- **LSP `Content-Length` body capped at 16 MiB**
  (`lsp/src/rpc/framing.cpp`). Previously accepted up to 4 GiB → trivial
  OOM via stdin from a hostile peer. Now fails with `HeaderError` so
  the dispatcher's read loop continues with the next message.
- **Input file size capped at 8 MiB** (`core/src/source.cpp`).
  Overridable via `HLSL_CLIPPY_MAX_FILE_BYTES` env var. Bounds memory
  cost on attacker-controlled shaders.
- **Slang prebuilt download SHA-256 verification.**
  `tools/fetch-slang.{sh,ps1}` now optionally verifies the downloaded
  tarball against `HLSL_CLIPPY_SLANG_SHA256_<UPPER_TRIPLE>` (or the
  generic `HLSL_CLIPPY_SLANG_SHA256`) env var. Mismatch refuses to
  populate the cache. Set the per-triple var in CI for hardened
  supply-chain; bumping `cmake/SlangVersion.cmake` should rotate the
  per-triple hashes from the Slang release-notes SHA-256 sums.
- **`tomlplusplus` `FetchContent` pinned to commit SHA**
  (`30172438cee64926dc41fdd9c11fb3ba5b2ba9de`, v3.4.0). Git tags are
  mutable; SHA pin defends against an upstream maintainer (or attacker
  with a stolen token) re-pointing v3.4.0 to a tampered tree.
- **CI submodule checkout switched from `recursive` to `true`**.
  `external/tree-sitter-hlsl/.gitmodules` references the `kajiya`
  renderer (Embark Studios) as a transitive submodule for grammar
  test fixtures — recursive checkout pulled hundreds of MB of code
  we never read at build time. Direct submodules only now.

### Added

- `SECURITY.md` rewrite — real disclosure channels (GitHub private
  vulnerability advisory link + maintainer email backup),
  90-day disclosure policy detail, supported-version table updated to
  v0.5.x, threat model section noting v0.5 hardening items
  (file-size cap, Slang download SHA-verify, no fuzz harness),
  and a hardening backlog section.
- `NOTICE` lists toml++, nlohmann/json, vscode-languageclient with
  one-liners (legal audit caught these as missing).
- `THIRD_PARTY_LICENSES.md` gains a full `## toml++ (MIT)` section
  reproducing the upstream MIT text.

### Fixed

- `[TODO: maintainer contact]` placeholders in `CONTRIBUTING.md` and
  `CODE_OF_CONDUCT.md` replaced with the maintainer email + a pointer
  to `SECURITY.md` for security-sensitive concerns.
- `[TODO: security contact]` placeholder in `SECURITY.md` replaced
  with the GitHub private-advisory channel.
- `vscode-extension/README.md` Requirements + Installation rewrite
  — drops the outdated "5c status note" warning + "(planned for v0.5
  launch)" headers; documents v0.5.3+ per-platform `.vsix` bundling.
- `vscode-extension/src/server-binary.ts` module-level comment
  refreshed: step 3 (bundled binary) is the primary hit since v0.5.3,
  steps 4+5 (cache + download) survive only as fallbacks.

### Removed

- `left_works/` directory (committed). Audit caught that the 7
  markdown files leaked absolute paths from a different machine
  (`c:/Users/vinle/...`), exposing the maintainer's other-machine
  username — internal-process detritus from the v0.5 handoff that's
  irrelevant post-launch.

## [0.5.3] — 2026-05-01

Two threads bundled into one tag: (1) finish off the same-day
release-pipeline triage chain, (2) ship per-platform .vsix bundling
so VS Code Marketplace users get a working extension without a
network download on first activation.

### Changed

- **VS Code extension now ships with the LSP binary bundled.**
  `release-vscode.yml` is now a 3-platform matrix
  (`ubuntu-latest` / `windows-latest` / `macos-14`) that builds the
  LSP server on each runner, drops it into
  `vscode-extension/server/<platform>/`, and packages a
  per-platform `.vsix` via `vsce package --target <vscode-target>`.
  Three `.vsix` files ship per release:
  - `hlsl-clippy-0.5.3-linux-x64.vsix`
  - `hlsl-clippy-0.5.3-win32-x64.vsix`
  - `hlsl-clippy-0.5.3-darwin-arm64.vsix`

  The Marketplace serves each user the matching `.vsix` for their
  OS+arch automatically. The TS-side `findBundled()` resolver
  (`vscode-extension/src/server-binary.ts`) already looked at
  `<extension>/server/<currentPlatform()>/<binary>` for the
  bundled path; with these per-platform `.vsix` files that path
  now resolves on first activation, no GitHub-Releases download
  required (firewall-friendly).

  A separate `publish` job depends on the matrix and runs
  `vsce publish --packagePath <vsix>` for each per-platform
  artifact when `VSCE_PAT` is set.

### Fixed

- Three more dead static helpers killed by Clang
  `-Wunused-function -Werror` on Linux + macOS, all lurking from
  template-style copy-paste in earlier rule packs:
  - `core/src/rules/clip_from_non_uniform_cf.cpp`: `is_id_char()`
  - `core/src/rules/groupshared_uninitialized_read.cpp`:
    `node_kind()` and `node_text()`
  - `core/src/rules/texture_lod_bias_without_grad.cpp`:
    `is_id_char()`

  Same MSVC-vs-Clang asymmetry as the v0.5.2 `trim()` fix — Windows
  CI never tripped on these. Sweep ran across every
  `core/src/rules/*.cpp` to catch the rest in one pass; sweep is
  clean post-fix.

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

[0.5.6]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.5...v0.5.6
[0.5.5]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.4...v0.5.5
[0.5.4]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.3...v0.5.4
[0.5.3]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.2...v0.5.3
[0.5.2]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/NelCit/hlsl-clippy/releases/tag/v0.5.0
