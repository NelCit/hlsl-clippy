# hlsl-clippy roadmap

A linter for HLSL — performance and correctness rules beyond what `dxc` catches.

**Status as of 2026-04-30:** Phase 0 + Phase 1 complete. The linter ships three rules end-to-end (`pow-const-squared`, `redundant-saturate`, `clamp01-to-saturate`) with machine-applicable `--fix` rewrites, inline `// hlsl-clippy: allow(...)` suppressions, and a `.hlsl-clippy.toml` config (toml++ v3.4.0) supporting per-rule severity, includes/excludes, and per-directory `[[overrides]]`. The Catch2 v3 unit-test suite passes 46/46 under MSVC `/W4 /WX /permissive-` (compiler floor: MSVC 14.44 / VS 17.14 / Build Tools 19.44). Phase 2 implementation plan is queued (ADR 0009: 24 rules across math + saturate-redundancy + misc; parallelizable as a shared-utilities PR + 3 per-category packs). ADR 0010 queues a 36-rule SM 6.7/6.8/6.9 expansion pack (SER, Cooperative Vectors, Long Vectors, OMM, Mesh-in-WG nodes) for Phase 3+.

## What's shipped (Phase 0)

- `pow-const-squared` rule: parses real HLSL, walks the tree-sitter AST, emits a rustc-style diagnostic with source span (diagnostic only; machine-applicable fix lands in Phase 1).
- tree-sitter + tree-sitter-hlsl vendored (`v0.26.8`, grammar at `bab9111`); `tools/treesitter-smoke/` smoke test.
- Slang vendored as git submodule (`v2026.7.1`); `tools/slang-smoke/` smoke test; `cmake/UseSlang.cmake`.
- CI: Windows (MSVC) + Linux (Clang) via `.github/workflows/ci.yml`; lint gate (`.github/workflows/lint.yml`); CodeQL stub (`.github/workflows/codeql.yml`).
- Hardened build: `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (Clang/GCC) scoped to project targets via `hlsl_clippy_warnings` INTERFACE library (`b58fa99`); vendored dependencies compile under their own flags.
- `.clang-tidy` with C++ Core Guidelines check set; `tests/.clang-tidy` scoped separately; CI fails on diagnostics.
- Beachhead corpus: 17 permissively-licensed shaders across vertex/pixel/compute/raytracing/mesh stages under `tests/corpus/`.
- License: Apache-2.0 (switched from MIT per ADR 0006); `NOTICE` and `THIRD_PARTY_LICENSES.md` added.
- 7 architecture decision records (`docs/decisions/0001–0007`) covering parser choice, Slang integration, module layout, devops, licensing, and naming.
- Phase 1 and Phase 2 implementation plans committed as ADR 0008 and ADR 0009.
- 21 rule documentation pages under `docs/rules/` (pow-const-squared + 12 math-category + 5 saturate-redundancy + 3 misc); catalog pre-populated for Phase 2.
- Blog stub live under `docs/blog/` (VitePress); first post on `pow-const-squared` (~1500 words at `docs/blog/pow-const-squared.md`).
- Governance files: `CONTRIBUTING.md` (DCO sign-off, conventional commits), `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`.
- `.github/PULL_REQUEST_TEMPLATE.md` and `ISSUE_TEMPLATE/{bug_report,rule_proposal}.yml`.
- `cli/` and `core/` directory layout (renamed from `crates/`).
- MSVC compiler floor pinned to MSVC 14.44 / Build Tools 19.44 / VS 17.14.

## What's shipped (Phase 1)

- Inline suppression parser — `// hlsl-clippy: allow(rule-id)` (line, block, file scope)
- Declarative TSQuery wrapper for AST-pattern rules
- Quick-fix `Rewriter` framework + `--fix` CLI flag (idempotent application)
- Two new rules with machine-applicable fixes: `redundant-saturate`, `clamp01-to-saturate`
- `.hlsl-clippy.toml` config (toml++ v3.4.0 single-include via FetchContent) — `[rules]`, `[includes]`, `[excludes]`, `[[overrides]]`; walk-up resolver bounded by `.git/`; `--config <path>` CLI flag
- Catch2 v3.5.4 test suite — 46/46 passing
- 22 hand-written fixture files across `tests/fixtures/{phase2,phase3,phase4,phase7}/` covering 76 unique rules with 116 `// HIT(...)` and 68 `// SHOULD-NOT-HIT(...)` annotations
- Corpus expanded to 27 public-licensed shaders (`tests/corpus/SOURCES.md` registry per ADR 0006)

## North star

`hlsl-clippy lint shaders/` produces actionable warnings on patterns that hurt GPU performance or hide correctness bugs (`pow(x, 2.0)`, derivatives in divergent control flow, missing `NonUniformResourceIndex`, etc.).

Output is human-readable for IDE use, JSON for CI gates, and quick-fixable where possible.

## Architecture

- **AST**: tree-sitter-hlsl (ADR 0002), patched as needed. Drives syntactic rules and is the substrate for CFG / data flow.
- **Compile + reflection + IR**: Slang (ADR 0001). Validates shaders, supplies type info / resource bindings / cbuffer layouts via reflection, and emits DXIL / SPIR-V for IR-level rules.
- **Diagnostic + fix engine**: in-tree, rustc/clippy-style spans with optional machine-applicable fixes (quick-fix Rewriter lands in Phase 1; see ADR 0008).
- **Frontend**: CLI for v0.x, LSP server for v0.5+.

## Code standards

- C++20 baseline. Compiler floors: MSVC 14.44 / VS 17.14, Clang 18+, GCC 14+.
- Build under MSVC `/W4 /WX /permissive-` and Clang/GCC `-Wall -Wextra -Wpedantic -Werror`. CI fails on any warning. Scoped to first-party targets via `hlsl_clippy_warnings` INTERFACE library.
- C++ Core Guidelines enforced via `clang-tidy` with `cppcoreguidelines-*`, `bugprone-*`, `modernize-*`, `performance-*`, `readability-*` check sets. `.clang-tidy` committed; CI runs it (ADR 0004).
- No raw `new`/`delete` outside explicit ownership boundaries. RAII everywhere.
- `clang-format` enforced. Single style; no bikeshedding.

## Licensing

- **Code**: Apache-2.0 (ADR 0006). Matches Slang upstream; patent grant matters for GPU-compilation tooling.
- **Documentation and blog posts**: CC-BY-4.0.
- **`tests/corpus/`**: each file retains its upstream license (Apache/MIT/CC0 only). Provenance tracked in `tests/corpus/SOURCES.md`.
- **Contributions**: DCO (Signed-off-by), not a CLA.
- Required files in repo: `LICENSE`, `NOTICE`, `THIRD_PARTY_LICENSES.md`.

## Phases

### Phase 0 — First real diagnostic (≈2 weeks) — COMPLETE

Goal: lint a real shader file end-to-end with one rule.

- [x] CMake project, C++20, CLI binary stub
- [x] CMake hardened: `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (Clang/GCC); scoped to first-party targets via `hlsl_clippy_warnings` INTERFACE library (`b58fa99`)
- [x] `.clang-tidy` committed with C++ Core Guidelines check set; `tests/.clang-tidy` scoped separately; CI fails on tidy diagnostics
- [x] Slang vendored as git submodule + linked as library; `tools/slang-smoke/` smoke test compiles an HLSL string and surfaces Slang diagnostics (`cmake/UseSlang.cmake`, `v2026.7.1`)
- [x] tree-sitter + tree-sitter-hlsl integrated; `tools/treesitter-smoke/` smoke test parses an HLSL file and walks the tree (`cmake/UseTreeSitter.cmake`, `v0.26.8`, grammar at `bab9111`)
- [x] First rule: `pow-const-squared` producing a diagnostic with span on a real shader (diagnostic only; machine-applicable fix lands in Phase 1 alongside the Rewriter framework)
- [x] Beachhead corpus picked and committed under `tests/corpus/` (17 permissively-licensed shaders across vertex/pixel/compute/raytracing/mesh stages)
- [x] CI on Windows (MSVC) + Linux (Clang) (`.github/workflows/ci.yml`, `.github/workflows/lint.yml`, `.github/workflows/codeql.yml` stub)
- [x] Blog stub live, first post drafted alongside `pow-const-squared` (VitePress under `docs/blog/`; `docs/blog/pow-const-squared.md` ~1500 words)
- [x] Rename `crates/cli` and `crates/core` → `cli/` and `core/`

Additional Phase 0 work landed:

- [x] C++ Core Guidelines enforcement via `clang-tidy`: curated check set (`cppcoreguidelines-*`, `bugprone-*`, `modernize-*`, `performance-*`, `readability-*`) with per-target enforcement (ADR 0004)
- [x] License switched MIT → Apache-2.0; `NOTICE` and `THIRD_PARTY_LICENSES.md` added (ADR 0006); MSVC compiler floor pinned to MSVC 14.44 / Build Tools 19.44 / VS 17.14
- [x] Architecture decision records: `docs/decisions/0001–0007` (parser, Slang integration, module layout, devops, licensing, naming) plus Phase 1 plan (ADR 0008) and Phase 2 plan (ADR 0009)
- [x] 21 rule documentation pages under `docs/rules/` (pow-const-squared + 12 math-category + 5 saturate-redundancy + 3 misc); catalog pre-populated for Phase 2 implementation
- [x] `docs/` site tree: `_template.md`, `index.md`, `getting-started.md`, `configuration.md`, `architecture.md`, `lsp.md`, `ci.md`, `contributing.md`
- [x] Governance files: `CONTRIBUTING.md` (DCO sign-off, conventional commits), `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1), `SECURITY.md`, `CHANGELOG.md` (Keep a Changelog 1.1.0)
- [x] `.github/PULL_REQUEST_TEMPLATE.md` and `ISSUE_TEMPLATE/{bug_report,rule_proposal}.yml`

### Phase 1 — Rule engine + quick-fix infrastructure (2-3 weeks) — COMPLETE

- [x] `Rule` interface + tree-sitter visitor harness; declarative s-expression query helper
- [x] Diagnostic format: file, span, code, severity, message, optional fix (rustc-style)
- [x] Quick-fix Rewriter: range-based source rewriter built on tree-sitter spans; machine-applicable fixes for `pow-const-squared` and other safe rewrites
- [x] Inline suppression: `// hlsl-clippy: allow(rule-name)` (line-scoped) and block-scoped variants
- [x] Config file: `.hlsl-clippy.toml` for rule severity, includes/excludes, per-directory overrides
- [x] Three rules total, each with quick-fix where safe and a blog post: `pow-const-squared`, `redundant-saturate`, `clamp01-to-saturate`

### Phase 2 — AST-only rule pack (3-4 weeks)

Rules expressible as tree-sitter AST patterns — no flow analysis. Rule catalog pre-populated in `docs/rules/`; implementation plan in ADR 0009.

- [ ] `pow-to-mul`: `pow(x, 2.0)` → `x*x`
- [ ] `length-squared`: `length(v) < r` → `dot(v,v) < r*r`
- [ ] `redundant-saturate`: `saturate(saturate(x))`
- [ ] `unused-cbuffer-field`
- [ ] `dead-store-sv-target`

### Phase 3 — Data flow (3-4 weeks)

Rules needing control / data flow:

- [ ] `loop-invariant-sample`: texture sample inside loop with loop-invariant UV
- [ ] `non-uniform-resource-index`: dynamic resource index missing the marker
- [ ] `redundant-computation-in-branch`

### Phase 4 — DXIL-level analysis (4+ weeks)

Hardest rules — operate on compiled IR via Slang reflection / DXIL:

- [ ] `derivative-in-divergent-cf`: `ddx`/`ddy`/`Sample` inside non-uniform control flow
- [ ] `vgpr-pressure-warning`: threshold-based register count warning per shader stage
- [ ] `redundant-texture-sample`: duplicate sample of same UV+texture

### Phase 5 — IDE integration (2-3 weeks)

- [ ] LSP server (small JSON-RPC layer in C++)
- [ ] VS Code extension
- [ ] Quick-fix surfaced as VS Code code actions

### Phase 6 — Launch

- [ ] CI gate mode: exit codes, JSON output, GitHub Actions reporter
- [ ] Documentation site: one page per rule (why it matters, before/after, generated ASM diff)
- [ ] Launch posts: graphics-programming Discord, r/GraphicsProgramming, Hacker News, Twitter
- [ ] Companion blog: one post per rule explaining the GPU reason it matters

## Non-goals (for now)

- Replacing vendor analyzers (RGA, Intel Shader Analyzer, Nsight). They have ground truth on their own ISA; we surface portable patterns.
- GLSL / WGSL support. Different ecosystems, different rules. Maybe later.
- Auto-fixing every rule. Some fixes (e.g. `length` → `dot`) need type/intent inference; ship as suggestions, not auto-fixes.

## Open questions

- **tree-sitter-hlsl v0.26.8 grammar gap on `cbuffer X : register(b0)`** — confirmed. The published grammar does not parse the explicit register-binding suffix on `cbuffer` declarations and produces an `ERROR` node. Plan: patch upstream as we hit this and other modern-HLSL gaps. Worst-case fallback: hand-rolled parser for the subset we need. See ADR 0002.
- **Slang on macOS.** Linux + Windows stable; macOS CI deferred until Phase 5.
- **DXIL vs SPIR-V for IR rules.** Slang emits both. Prototype IR rules on SPIR-V first; DXIL is the D3D12 deployment target.
- **DXC in PATH at runtime.** `slang-smoke` requires DXC at runtime on some paths. Determine whether to bundle DXC in release artifacts or require it on PATH. See ADR 0005.
- **Linux distro floor / libc selection** — Ubuntu 24.04 (glibc 2.39) is the CI baseline; whether to add a `manylinux_2_28` build container before v0.1 is open. libc++ vs libstdc++ on the Clang job TBD. See ADR 0005.
- **Slang version pinning.** Reflection API is more stable than compiler internals but still ABI-fluid across releases. Pin a release, bump deliberately; CI catches breakage.
- **Module decomposition** (`include/hlslc/` + `libs/{parser,semantic,diag,rules,driver}/` + `apps/{cli,lsp}/`). Finer split than current `cli/` + `core/`. Tracked as ADR 0003 (Proposed) — defer until Phase 1+.
- **Static binary distribution.** Single static binary per OS via GitHub releases. Slang as static lib if its build allows; otherwise ship the shared library alongside.
