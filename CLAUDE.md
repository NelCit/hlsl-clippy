# CLAUDE.md

This file gives Claude Code (claude.com/claude-code) and other AI coding
assistants durable project context when working in this repo. Human
contributors should also find it useful as a quick orientation.

For the public-facing project description, see [README.md](README.md). For
the implementation roadmap, see [ROADMAP.md](ROADMAP.md). For canonical
decisions, see [docs/decisions/](docs/decisions/).

---

## What this project is

`hlsl-clippy` is a static linter for HLSL written in C++23, built on
tree-sitter-hlsl (AST) and Slang (compile + reflection + IR). It surfaces
portable anti-patterns that hurt GPU performance or hide correctness bugs —
patterns that `dxc` and vendor analyzers do not flag. Phases 0 → 5 are
complete and v0.5.3 has shipped with **154 rules** end-to-end across math,
bindings, texture, workgroup, control-flow, mesh, DXR, work-graphs, SER,
cooperative-vector, long-vectors, opacity-micromaps, sampler-feedback,
VRS, and wave-helper-lane — plus machine-applicable `--fix` rewrites,
inline suppressions, a `.hlsl-clippy.toml` config, an LSP server, and a
VS Code extension. A companion blog series (9 launch posts) explaining
the GPU reasoning behind each rule category is central to the project's
reputation goal.

---

## Current status (as of 2026-05-02, v0.5.6 shipped + Phase 6+ hardening landed)

- **Phases 0 → 6 complete.** `cli/`, `core/`, `lsp/`, `vscode-extension/`
  all ship; CI matrix on Linux + Windows + macOS; binary release pipeline
  fully green on v0.5.6 (Linux/Windows/macOS CLI+LSP archives + per-platform
  `.vsix` for VS Code Marketplace). All four GitHub Actions workflows (CI,
  Lint, Docs, CodeQL) green on tip-of-main `8d44934`.
- **154 rules registered + tested.** 154 `.cpp` rule files, 154
  `registry.cpp` factory entries, 154 `tests/unit/test_*.cpp` per-rule
  tests. **ctest baseline 672 / 672** on Windows clang-cl + libstdc++
  (resolved 2026-05-02; see `tests/KNOWN_FAILURES.md` for the historical
  CRLF + snapshot-drift triage chain). 186 rule pages under `docs/rules/`,
  of which 32 are doc-only stubs (rule pages for ADR 0007 / 0010 / 0011
  rules queued for Phase 7+).
- **15 ADRs landed** (`docs/decisions/0001`–`0015`). ADR 0010 queues 36
  SM 6.7/6.8/6.9 rules (SER, Cooperative Vectors, Long Vectors, OMM,
  Mesh Nodes) — most have shipped through Phase 3 sub-phase 3c. ADR
  0015 plans the v0.5.0 launch (sub-phases 6a + 6b + 6c shipped;
  6d/6e/6f/6g maintainer-driven).
- **C++23 baseline shipped** (sub-phase 3a, commit `1ea2aaa`):
  `CMakeLists.txt` line 10 sets `CMAKE_CXX_STANDARD 23`; per-target
  `target_compile_features(... PUBLIC cxx_std_23)` so vendored Slang
  / tree-sitter keep their own standard.
- **Phase 6+ hardening (v0.6 prep) burned down 2026-05-02.** Shipped this
  pass (commits `7afe88b` → `8d44934`):
  - `core/src/rules/util/ast_helpers.{hpp,cpp}` factor — 1422 LOC removed
    across 119 rule TUs.
  - LSP static-lib factor (`hlsl_clippy_lsp_lib`).
  - Slang prebuilt cache step in every workflow.
  - Parallel `clang-tidy` via `run-clang-tidy-18 -j$(nproc)`; lint went
    from ~30 min serial to ~10 min parallel.
  - `.clang-tidy` suppressions broadened for clang-tidy 18 noise +
    `EnumConstantCase: CamelCase`.
  - Coverage gate (Linux Clang + `llvm-cov` + Codecov) — `ci.yml`
    `coverage` job; threshold deferred until baseline data lands.
  - Nightly bench harness (`bench.yml`); trend-comparison artifact
    diffing deferred.
  - `.gitattributes` LF-pin on goldens + CRLF-tolerant test compare.
  - CFG engine `build_with_tree` overload — reuses parsed tree-sitter
    tree, ~5–15 % lint-time saving per source.
  - LSP serves hover + code-action from `OpenDocument::latest_diagnostics`
    instead of re-running `lint()` per request.
  - All 4 STATUS_STACK_BUFFER_OVERRUN goldens resolved (root causes:
    Slang reflection emitting absolute paths in messages, sort-key
    tie-break, fixture/Slang version drift). Snapshot harness now
    filters `clippy::*` infrastructure diagnostics + uses 4-key sort.
  - `docs/rules/*.md` banner refresh — 134 pages updated from
    "pre-v0 — scheduled" / "Pre-v0 status" to "shipped (Phase N)";
    183 companion-blog-link placeholders linked to the shipped per-
    category overview posts.
  - Docs site live at https://nelcit.github.io/hlsl-clippy/; 9 launch
    blog posts shipped (preface + 8 category overviews + 1 per-rule);
    per-platform `.vsix` bundling lands in v0.5.3. Slang submodule
    retired in commit 73c0322 — Slang is now consumed via the prebuilt
    cache populated by `tools/fetch-slang.{sh,ps1}` (or `Slang_ROOT`
    for power users).

---

## Locked technical decisions

One line per decision + ADR link. Do not re-litigate these without reading
the ADR first.

- **Compiler**: Slang — cross-platform compile, reflection, IR (DXIL +
  SPIR-V + Metal + WGSL). Do NOT re-propose DXC, in-tree clang HLSL, or
  any alternative. See
  [ADR 0001](docs/decisions/0001-compiler-choice-slang.md).

- **Parser**: tree-sitter-hlsl, vendored as git submodule, built as OBJECT
  lib. Public API exposes `(SourceId, byte-lo, byte-hi)` spans only — never
  `TSNode`. Note: known grammar gap on `cbuffer X : register(b0)` (see
  "Known issues" below). See
  [ADR 0002](docs/decisions/0002-parser-tree-sitter-hlsl.md).

- **Module decomposition**: current `cli/` + `core/` + `lsp/` +
  `vscode-extension/` split is what is shipped. The more granular
  `apps/` + `libs/` + `include/` layout per
  [ADR 0003](docs/decisions/0003-module-decomposition.md) remains
  **Proposed** — the 2026-05-01 architecture audit found "no concrete
  harm of staying with the current layout"; do not restructure the
  source tree without user approval.

- **C++ baseline**: C++23 + selective C++26. Compiler floors: MSVC 19.44+
  (VS 17.14 / Build Tools 14.44), Clang 18+ with libc++ 17+ or libstdc++
  13+, GCC 14+. clang-tidy 19+. CMakeLists.txt now sets
  `CMAKE_CXX_STANDARD 23` (uplifted in sub-phase 3a, commit `1ea2aaa`).
  **Validated locally against MSVC 19.50.35730 (VS 18 / 2026 Community)**
  + Slang prebuilt cache as of 2026-05-01; full `core/`/`cli/`/`lsp/`
  build is clean. Linux Clang 18 + libstdc++ 13 also exercised in CI.
  See [ADR 0004](docs/decisions/0004-cpp23-baseline.md).

- **CI/CD**: `windows-2022` + `ubuntu-24.04` GHA matrix; sccache; 3-tier
  cache (Slang install-prefix / sccache / CMake configure); Catch2 v3;
  no CPack (use `softprops/action-gh-release@v2` for releases). macOS
  deferred to Phase 5. See
  [ADR 0005](docs/decisions/0005-cicd-pipeline.md).

- **License**: Apache-2.0 (code), CC-BY-4.0 (docs/blog), DCO sign-off on
  every commit, no CLA. Required files present: [LICENSE](LICENSE),
  [NOTICE](NOTICE),
  [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). See
  [ADR 0006](docs/decisions/0006-license-apache-2-0.md).

- **Rule-pack expansion**: 41 additional rules (SM 6.4 → 6.8) distributed
  across Phases 2–7. Canonical list in ADR 0007; do not add rules to the
  roadmap without an ADR or addendum. See
  [ADR 0007](docs/decisions/0007-rule-pack-expansion.md).

- **Phase 1 engine design**: TSQuery RAII wrapper, `Rewriter`, `Fix`,
  `TextEdit`, `SuppressionSet`, `Config` (toml++ v3.4.0). Design is shipped
  and confirmed. See
  [ADR 0008](docs/decisions/0008-phase-1-implementation-plan.md).

- **Phase 2 plan**: per-category packs (math / saturate-redundancy / misc);
  shared-utilities PR first; 24 net-new rules. Three implementers can work
  in parallel after shared utilities land. See
  [ADR 0009](docs/decisions/0009-phase-2-implementation-plan.md).

- **SM 6.9 rule expansion**: 36 additional rules (SER, Cooperative Vectors,
  Long Vectors, OMM, Mesh Nodes, SM 6.7/6.8 surfaces) distributed across
  Phases 2–7. Preview mesh-node rules gated behind
  `[experimental] work-graph-mesh-nodes = true` in `.hlsl-clippy.toml`. See
  [ADR 0010](docs/decisions/0010-sm69-rule-expansion.md).

- **Candidate rule adoption (per-phase plan)**: 41 additional LOCKED rules
  across underexplored portable surfaces (groupshared / LDS micro-arch,
  ByteAddressBuffer alignment, samplers, root-signature ergonomics, mesh
  extras, compute-pipeline shape, numerical / precision, wave-quad extras,
  texture-format, divergence hints) — 6 Phase 2, 17 Phase 3, 16 Phase 4,
  2 Phase 7 — plus 20 DEFERRED and 2 DROPPED candidates. **Accepted**;
  per-phase plans mirror ADR 0009's shared-utilities-PR + parallel-pack
  pattern. See
  [ADR 0011](docs/decisions/0011-candidate-rule-adoption.md).

- **Phase 3 reflection infrastructure**: opaque `reflection.hpp` public
  header + new `Stage::Reflection` + `Rule::on_reflection` virtual + lazy
  per-(SourceId, target-profile) cached `ReflectionEngine` (one global
  Slang `IGlobalSession`, per-worker `ISession` pool); `<slang.h>`
  confined to `core/src/reflection/slang_bridge.cpp`. **Accepted**;
  gates ALL ~55 Phase 3 rules across ADR 0007 / 0010 / 0011. Lands as
  sub-phase 3a (infra PR) → 3b (shared utilities) → 3c (5 parallel
  rule packs). See
  [ADR 0012](docs/decisions/0012-phase-3-reflection-infrastructure.md).

- **Phase 4 control-flow / data-flow infrastructure**: opaque
  `control_flow.hpp` public header + new `Stage::ControlFlow` +
  `Rule::on_cfg` virtual + lazy per-`SourceId` cached `CfgEngine` (CFG
  built over tree-sitter AST, Lengauer-Tarjan dominator tree, taint-
  propagation uniformity oracle, helper-lane analyzer, bounded
  inter-procedural inlining at `cfg_inlining_depth = 3`); ERROR-node
  tolerance per ADR 0002. **Accepted**; gates ALL ~45 Phase 4 rules
  across ADR 0007 §Phase 4 / ADR 0010 §Phase 4 / ADR 0011 §Phase 4.
  Does NOT depend on ADR 0012 / Phase 3 — CFG works without
  reflection. Lands as sub-phase 4a (infra PR) → 4b (shared utilities)
  → 4c (5 parallel rule packs). See
  [ADR 0013](docs/decisions/0013-phase-4-control-flow-infrastructure.md).

- **Phase 5 LSP + IDE architecture**: separate `lsp/` C++ binary
  (`hlsl_clippy_lsp`) thin-wrapping `core` over JSON-RPC (nlohmann/json
  vendored, in-tree dispatcher); TypeScript VS Code extension
  (`vscode-extension/`, Apache-2.0, `nelcit` publisher) thin-wrapping
  the LSP; quick-fixes surfaced as `quickfix` code actions; per-document
  `.hlsl-clippy.toml` walk-up reused; macOS CI matrix added in the same
  phase. **Accepted**; lands as 5a (server scaffolding) → 5b (code
  actions) → 5c (extension, parallel-after-5a) → 5d (macOS CI,
  parallel-after-5a) → 5e (distribution). See
  [ADR 0014](docs/decisions/0014-phase-5-lsp-architecture.md).

- **Phase 6 launch plan (v0.5.0 release)**: tag directly from `main`
  after the release-readiness audit returns green; sub-phases 6a (CI
  gate-mode polish — `--format=github-annotations` + example workflow),
  6b (docs site polish — README links, demo gif, dead-link cleanup),
  6c (8 parallel category-overview blog posts in lieu of one-per-rule
  for v0.5), 6d (optional Marketplace publisher provisioning), 6e
  (final pre-tag audit), 6f (tag + release), 6g (staggered launch
  posts: Discord day 0, HN day 1, r/GraphicsProgramming day 2). The
  ~150 individual per-rule blog posts are a v0.6+ flywheel — v0.5
  ships category overviews. **Accepted**. See
  [ADR 0015](docs/decisions/0015-phase-6-launch-plan.md).

---

## Code standards (enforced by CI)

- **/W4 /WX /permissive-** on MSVC; **-Wall -Wextra -Wpedantic -Werror** on
  Clang/GCC. Warnings are errors. The `hlsl_clippy_warnings` INTERFACE
  library scopes these flags to first-party targets only — vendored Slang
  and tree-sitter compile under their own flags.

- **clang-tidy 19+** with `WarningsAsErrors: '*'`. CI fails on any
  diagnostic. Check sets enabled: `bugprone-*`, `cppcoreguidelines-*`,
  `modernize-*`, `performance-*`, `readability-*`, `portability-*`,
  `misc-*`. Several noisy checks are suppressed (see [.clang-tidy](.clang-tidy)
  for the exact list). `HeaderFilterRegex` covers `cli/`, `core/`, `src/`.

- **Naming conventions** (enforced by `readability-identifier-naming`):
  `lower_case` namespaces, functions, variables, parameters, constants;
  `CamelCase` classes, structs, enums; `UPPER_CASE` macros; `k_` prefix on
  global constants and constexpr variables; `_` suffix on private members.

- **clang-format** enforced. Single style file at root. No bikeshedding.

- **C++23 stdlib equivalents preferred over Microsoft GSL.** `std::span`
  for views, `gsl::not_null` deemed unnecessary (use references or bare
  pointers documented as non-null), `gsl::narrow_cast` deferred to
  static_cast with explicit assertions where needed. GSL was discussed
  in ADR 0006 + early CLAUDE.md drafts but never actually linked into
  the build; do not re-introduce it without an addendum ADR.

- **C++23 idioms to use**:
  - `std::expected<T, Diagnostic>` — canonical fallible-return type across
    rule and parser stages; no exceptions across the `core` API boundary.
  - `std::print` / `std::println` — diagnostic rendering and CLI output.
  - Deducing `this` — AST visitor base classes; drop CRTP.
  - `if consteval` — span/range utilities.
  - `[[assume]]` — hot loops, applied narrowly.
  - `std::flat_map` / `std::flat_set` — small rule registries, per-file
    suppression sets.

- **C++26 adopt-now (feature-test gated)**:
  - `std::inplace_vector` (`__cpp_lib_inplace_vector`)
  - Pack indexing / P2662
  - `=delete("reason")` / P2573

- **Ban list**: no exceptions across the `core` API boundary (use
  `std::expected`); no `std::endl` in hot paths; no `using namespace` at
  file scope; no C-style casts; no raw owning pointers; no implicit
  narrowing; no `goto`.

- **Coverage gate**: 60% line coverage on `core/` (Clang job only via
  llvm-cov). Target: 75% by Phase 2 completion, 80% by Phase 4 completion.

---

## Known issues to plan around

- **tree-sitter-hlsl v0.2.0 grammar gap — `cbuffer X : register(b0)`.**
  The grammar does not parse the explicit register-binding suffix on
  `cbuffer` declarations and produces an ERROR node. This affects any rule
  that needs cbuffer binding info from the AST alone. Fallback: Slang
  reflection knows the resource by name. Tracked in
  [ROADMAP.md — Open questions](ROADMAP.md) and confirmed in
  [ADR 0002](docs/decisions/0002-parser-tree-sitter-hlsl.md). Patches
  to the upstream grammar are welcome.

- **Additional grammar gaps**: `[numthreads(...)]` attribute + function
  declaration combos may produce error nodes in some forms; struct member
  semantics (`: POSITION`, `: SV_Target`) have edge-case gaps. See
  [external/treesitter-version.md](external/treesitter-version.md) for the
  full list observed during smoke testing.

- **Slang not thread-safe at `IGlobalSession` level.** Maintain one global
  session and a per-worker `ISession` pool.

- **macOS CI added in Phase 5 (sub-phase 5d, ADR 0014).** Tested on
  `macos-14` (Apple Silicon). Linux + Windows CI remain stable. Known
  limitation: the Slang macOS prebuilt may have format differences vs a
  source build (Slang/Metal paths have historically been rocky) — track
  here if surfaces change. Stop-gap path per ADR 0014's
  "Risks & mitigations": ship a no-reflection macOS build for v0.5 if the
  Slang/macOS surface is broken on the pinned version, then roll the full
  path into v0.6.

- **CMakeLists.txt still sets `CMAKE_CXX_STANDARD 20`.** ADR 0004 locks
  C++23; the build-system edit is a tracked follow-up before Phase 2.

- **`<slang.h>` must never appear in `core/include/hlsl_clippy/` OR
  `core/src/rules/`.** CI grep enforces both. The only TU under `core/`
  allowed to include `<slang.h>` is `core/src/reflection/slang_bridge.cpp`;
  rules go through `RuleContext` / `ReflectionInfo` (ADR 0012). Same
  constraint applies to `tree_sitter/api.h` in public headers.

---

## Repo conventions

- **Conventional Commits 1.0** (`feat:`, `fix:`, `refactor:`, `docs:`,
  `chore:`, `test:`, `build:`, `ci:`). PR title format matches.
- **DCO**: every commit signed off with `Signed-off-by:`. Use `git commit -s`.
  No CLA. PRs with unsigned commits are not merged.
- **Branch model**: `feat/<desc>`, `fix/<desc>`, `docs/<desc>`,
  `refactor/<desc>`, `build/<desc>`. Implementation work lives in git
  worktrees under `.claude/worktrees/`. Merge to `main` via `--no-ff`
  (or fast-forward for trivial single-file changes); do not leave branches
  stranded.
- **Rule + blog post pair**: when a rule lands, a companion blog post
  explaining the GPU mechanism lands with it. See `docs/blog/` and
  `docs/rules/`.
- **New rule checklist**: rule file in `core/src/rules/`, registry entry,
  Catch2 test in `tests/unit/rules/`, fixture in
  `tests/fixtures/phaseN/<category>.hlsl` with `// HIT(rule-id)` and
  `// SHOULD-NOT-HIT(rule-id)` annotations, and a doc page in
  `docs/rules/<rule-id>.md` using `docs/rules/_template.md`. PRs that add a
  rule without a doc page are not merged.
- **Release checklist**: see [tools/release-checklist.md](tools/release-checklist.md)
  for the canonical pre-tag steps (version bumps across `core/src/version.cpp`,
  `vscode-extension/package.json`, and `CHANGELOG.md`; local clean build;
  tag push triggers `.github/workflows/release.yml` + `release-vscode.yml`).
- **Docs site:** `docs/` is built by VitePress; pushed to `gh-pages` branch
  via `.github/workflows/docs.yml` on every main commit touching `docs/**`.
  Live at https://nelcit.github.io/hlsl-clippy/.
- **Pre-commit hook**: install the project clang-format gate via
  `pwsh tools/install-hooks.ps1` (Windows) or `bash tools/install-hooks.sh`
  (Linux / macOS) — runs `clang-format --dry-run --Werror` on staged
  `cli/` / `core/` / `tools/` / `lsp/` C++ files, matching the
  `.github/workflows/lint.yml` glob. See
  [tools/git-hooks/README.md](tools/git-hooks/README.md) for env-var
  overrides (`HLSL_CLIPPY_HOOK_FIX=1` to auto-fix + re-stage).

---

## Working in this repo with an AI assistant

These are the patterns that work well on this codebase:

### Worktree-from-main; close the merge loop

Implementation work goes in an isolated git worktree branched from `main`.
Do not strand branches — either have the agent merge itself for
non-conflicting work (single-file or disjoint-tree changes), or batch
finished branches in a follow-up merge agent. Phase boundaries are good
merge points: do not advance from Phase N to Phase N+1 with unmerged Phase N
branches outstanding.

After merge, clean up: `git worktree remove` + `git branch -d`.

### Parallelize rule-pack work; serialize engine-architecture work

N independent rule files / doc pages / fixtures can parallelize cleanly
(different files, no shared design surface). Cross-cutting engine pieces
— Rule interface, parser bridge, Rewriter, suppression parser, config loader
— share design surface; splitting them across parallel agents causes design
drift that surfaces at merge time.

The canonical pattern (from ADR 0009): ship the shared-utilities PR first in
one agent, then dispatch 3+ parallel per-category rule-pack agents.

### Clang-tidy at scale

Do not run full-project clang-tidy in a single dispatch. It can exceed
conservative no-output watchdogs. Run per-file with explicit timeouts; let
CI carry the full pass.

### Cite the ADR when touching a locked decision

License, compiler, parser, C++ standard — these are locked in ADRs. If a
proposed change touches one, cite the ADR, explain why the change is
compatible or why a new ADR is warranted.

### Fixture annotation conventions

Fixtures under `tests/fixtures/phaseN/` drive integration tests. Every line
that should produce a diagnostic must carry a `// HIT(rule-id)` comment;
every line that must stay clean under a similar but non-matching pattern
carries `// SHOULD-NOT-HIT(rule-id)`. Both kinds are required when adding or
modifying a rule — the test runner counts both forward and backward:
every HIT fires, every SHOULD-NOT-HIT stays silent.

### Doc page front-matter (per `docs/rules/_template.md`)

Required fields: `rule_id`, `category`, `phase`, `applicability`
(`machine-applicable | suggestion | none`), `gpu_reason` (one paragraph —
mandatory; this is the blog-post seed). PRs that omit `gpu_reason` are
blocked at review. The category list as of Phase 1:
`math`, `bindings`, `texture`, `workgroup`, `control-flow`, `vrs`,
`sampler-feedback`, `mesh`, `dxr`, `work-graphs`, `ser`,
`cooperative-vector`, `long-vectors`, `opacity-micromaps`,
`wave-helper-lane`.

### Build from source (quick reference)

```sh
# Clone with submodules
git clone --recurse-submodules https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy

# Configure + build (debug, with compile_commands.json for clangd)
cmake -B build-debug --preset dev-debug
cmake --build build-debug

# Run tests
ctest --test-dir build-debug --output-on-failure

# Lint a shader
./build-debug/hlsl-clippy lint shader.hlsl
./build-debug/hlsl-clippy lint --fix shader.hlsl
./build-debug/hlsl-clippy lint --format=json shader.hlsl
```

**Windows shortcut: dot-source `tools\dev-shell.ps1`** before running cmake / ninja / ctest. It locates the latest VS install via `vswhere`, enters the VS Dev Shell (puts `cl.exe`, `link.exe`, `INCLUDE`, `LIB` on PATH), prepends VS-bundled `cmake.exe` + `ninja.exe`, and adds the Slang prebuilt cache's `bin/` so test exes resolve `slang.dll` + the 6 transitive runtime DLLs at runtime. Idempotent (`HLSL_CLIPPY_DEV_SHELL_READY` guard). Replaces ~30 lines of manual env munging per build attempt.

```powershell
. .\tools\dev-shell.ps1                                 # one-time per session
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release       # then run cmake / ninja / ctest directly
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with MSVC, use `--preset ci-msvc` or configure without a preset
and let CMake pick MSVC. The `hlsl-clippy_warnings` INTERFACE library
applies `/W4 /WX /permissive-` automatically to first-party targets.

### Slang prebuilt cache (local dev)

Building Slang from source takes ~20 minutes cold; every fresh worktree
(see "Worktree-from-main" above) would re-pay that cost. `cmake/UseSlang.cmake`
resolves Slang in this priority order:

1. **`Slang_ROOT`** (CMake var or env var) — install prefix with
   `include/slang.h`, `lib/`, `bin/`.
2. **Per-user prebuilt cache** at `%LOCALAPPDATA%/hlsl-clippy/slang/<version>/`
   (Windows) or `$HOME/.cache/hlsl-clippy/slang/<version>/` (Linux).
   Override root with `HLSL_CLIPPY_SLANG_CACHE`.
3. **Submodule from-source build** — historical default; runs when neither
   above hits. CI is unaffected (opt-in only).

Populate the cache once per machine:

```sh
pwsh tools/fetch-slang.ps1   # Windows
bash tools/fetch-slang.sh    # Linux
```

The cache is keyed by `HLSL_CLIPPY_SLANG_VERSION` in `cmake/SlangVersion.cmake`
(currently `2026.7.1`); bumping the submodule SHA invalidates the cache by
design — re-run the fetch script after a bump. ABI caveat: prebuilt must
match the submodule version exactly; mismatch fails at link time, fall back
to source build by clearing the cache. macOS support landed in Phase 5
(sub-phase 5d, ADR 0014): `tools/fetch-slang.sh` auto-detects Darwin via
`uname -s` and downloads `slang-<version>-macos-aarch64.tar.gz` (Apple
Silicon) or `slang-<version>-macos-x86_64.tar.gz` (Intel) into the same
`$HOME/.cache/hlsl-clippy/slang/<version>/` root that Linux uses.

---

## Phase status

| Phase | State | Notes |
|---|---|---|
| 0 — First real diagnostic | DONE | `pow-const-squared` end-to-end; smoke tools; CI; 17-shader corpus; 7 ADRs; blog stub |
| 1 — Rule engine + quick-fix | DONE | Suppression; declarative TSQuery; `Rewriter` + `--fix`; `.hlsl-clippy.toml` + `--config`; `redundant-saturate`; `clamp01-to-saturate`; 46/46 tests; 27-shader corpus; 22 expansion fixtures; 3 ADRs |
| 2 — AST-only rule pack | DONE | ADR 0009 shipped: math / saturate-redundancy / misc packs; 24 net-new rules |
| 3 — Reflection-aware | DONE | ADR 0012 reflection infra + ADR 0007/0010 rule packs landed (Pack A/B/C/D/E — 60 rules) |
| 4 — Control flow + light data flow | DONE | ADR 0013 CFG/uniformity infra + control-flow / atomics / wave-helper-lane packs landed; 42 rules; 12 wiring failures triaged 2026-05-01 |
| 5 — LSP + IDE | DONE | ADR 0014 sub-phases 5a→5e shipped: LSP server, code-actions, VS Code extension, macOS CI matrix, release pipeline |
| 6 — Launch (v0.5) | IN PROGRESS | docs site at https://nelcit.github.io/hlsl-clippy/ wired (Pages enable pending); release-readiness audit running; CI gate-mode + per-rule blog stubs queued |
| 7 — IR-level / stretch | PLANNED | Register pressure; redundant samples; packed-math precision; `live-state-across-traceray`; `maybereorderthread-without-payload-shrink` |

---

## ADR index

All 16 ADRs are in MADR 4.0 format under `docs/decisions/`. Each ADR's
`status` field is the canonical authority — read it before assuming a
decision is settled.

| ADR | Title | Status |
|---|---|---|
| [0001](docs/decisions/0001-compiler-choice-slang.md) | Compiler choice — Slang | Accepted |
| [0002](docs/decisions/0002-parser-tree-sitter-hlsl.md) | Parser — tree-sitter-hlsl | Accepted |
| [0003](docs/decisions/0003-module-decomposition.md) | Module decomposition | Proposed |
| [0004](docs/decisions/0004-cpp23-baseline.md) | C++23 baseline + selective C++26 adoption | Accepted |
| [0005](docs/decisions/0005-cicd-pipeline.md) | CI/CD pipeline | Accepted |
| [0006](docs/decisions/0006-license-apache-2-0.md) | License — Apache-2.0 (code) + CC-BY-4.0 (docs) + DCO | Accepted |
| [0007](docs/decisions/0007-rule-pack-expansion.md) | Rule-pack expansion (+41 rules) | Accepted |
| [0008](docs/decisions/0008-phase-1-implementation-plan.md) | Phase 1 implementation plan | Accepted |
| [0009](docs/decisions/0009-phase-2-implementation-plan.md) | Phase 2 implementation plan — AST-only rule pack | Accepted |
| [0010](docs/decisions/0010-sm69-rule-expansion.md) | SM 6.9 rule expansion (+36 rules) | Accepted |
| [0011](docs/decisions/0011-candidate-rule-adoption.md) | Candidate rule adoption — underexplored portable surfaces (per-phase plan) | Accepted |
| [0012](docs/decisions/0012-phase-3-reflection-infrastructure.md) | Phase 3 reflection infrastructure — Slang reflection plumbed into RuleContext | Accepted |
| [0013](docs/decisions/0013-phase-4-control-flow-infrastructure.md) | Phase 4 control-flow / data-flow infrastructure — CFG + uniformity oracle | Accepted |
| [0014](docs/decisions/0014-phase-5-lsp-architecture.md) | Phase 5 LSP + IDE architecture — JSON-RPC server + VS Code extension | Accepted |
| [0015](docs/decisions/0015-phase-6-launch-plan.md) | Phase 6 launch plan — v0.5.0 release | Accepted |
| [0016](docs/decisions/0016-phase-7-ir-infrastructure.md) | Phase 7 IR-level analysis infrastructure — DXIL engine + liveness + register-pressure | Proposed |

"Proposed" ADRs represent plans that are approved in principle but not yet
fully implemented. "Accepted" ADRs represent shipped decisions. Do not
amend an Accepted ADR to change a decision — add an addendum ADR instead.

---

## File map (top-level)

```
cli/                    hlsl-clippy executable (src/main.cpp)
lsp/                    hlsl-clippy-lsp executable (LSP server, ADR 0014)
vscode-extension/       VS Code extension wrapping the LSP server (TypeScript, ADR 0014)
core/                   static lib
  include/hlsl_clippy/  PUBLIC headers (version.hpp, diagnostic.hpp,
                          rule.hpp, rewriter.hpp, suppress.hpp, config.hpp)
  src/                  private implementation (parser bridge, rules,
                          query engine, rewriter, config, suppression)
cmake/                  UseSlang.cmake, UseTreeSitter.cmake, SlangVersion.cmake
external/               git submodules: Slang, tree-sitter, tree-sitter-hlsl,
                          tomlplusplus; treesitter-version.md (gap registry)
tools/                  slang-smoke/, treesitter-smoke/ dev utilities
tests/
  corpus/               27 public-licensed real shaders; per-file provenance
                          in tests/corpus/SOURCES.md
  fixtures/             hand-written HLSL with // HIT(rule) and
                          // SHOULD-NOT-HIT(rule) annotations, organised by
                          phase subdirectory
  unit/                 Catch2 v3 unit tests
  golden/               golden output snapshots
docs/
  decisions/            ADRs in MADR 4.0 format (0001–0015)
  rules/                per-rule pages; _template.md; pre-populated catalog
  blog/                 per-rule blog posts (CC-BY-4.0); VitePress scaffold
  architecture.md       high-level pipeline diagram
.github/workflows/      ci.yml, lint.yml, docs.yml, release.yml, release-vscode.yml
ROADMAP.md              phased plan + open questions
CHANGELOG.md            Keep a Changelog 1.1.0
CONTRIBUTING.md         DCO, conventional commits, rule authoring guide
CODE_OF_CONDUCT.md      Contributor Covenant 2.1
SECURITY.md
LICENSE                 Apache-2.0
NOTICE                  per-vendored-dep attribution
THIRD_PARTY_LICENSES.md full vendored license texts
```

---

## What NOT to do

- Do not propose alternatives to Slang (compiler), Apache-2.0 (license),
  C++23 (standard), or tree-sitter-hlsl (parser) without first reading the
  relevant ADR. These are deliberately locked.
- Do not silently restructure the source tree from `cli/` + `core/` to the
  `apps/` + `libs/` layout (ADR 0003 is Proposed, not Accepted).
- Do not add per-file SPDX headers unprompted — that is a future tracked
  task, not the current default.
- Do not mock the parser or Slang in tests — integration tests are the norm
  for this codebase.
- Do not push branches other than `main` to the public remote.
- Do not run `git push --force` on `main`.
- Do not assume `cbuffer X : register(b0)` parses correctly from tree-sitter
  alone — it does not; fall back to Slang reflection.
- Do not add rules to the roadmap without a corresponding ADR or addendum.
- Do not let `<slang.h>` or `<tree_sitter/api.h>` appear in
  `core/include/hlsl_clippy/`.

---

*This file is auto-loaded by Claude Code. To update durable project context,
edit this file. To update transient task state, use the session's todo list,
not this file.*
