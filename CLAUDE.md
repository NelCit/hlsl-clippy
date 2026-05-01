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
patterns that `dxc` and vendor analyzers do not flag. Phase 0 and Phase 1
are complete: the linter ships three end-to-end rules with machine-applicable
`--fix` rewrites, inline suppressions, and a `.hlsl-clippy.toml` config. A
companion blog series explaining the GPU reasoning behind each rule is central
to the project's reputation goal.

---

## Current status (as of 2026-04-30)

- **Phase 0 complete.** `pow-const-squared` end-to-end, tree-sitter + Slang
  vendored, CI on Windows + Linux, 17-shader corpus, 7 ADRs, blog stub.
- **Phase 1 complete.** Inline suppression parser, declarative TSQuery
  wrapper, quick-fix `Rewriter` + `--fix`, `.hlsl-clippy.toml` config
  (toml++ v3.4.0), two new rules (`redundant-saturate`,
  `clamp01-to-saturate`); 46/46 Catch2 tests passing; corpus expanded to 27
  shaders; 22 hand-written expansion fixture files.
- **10 ADRs landed** (`docs/decisions/0001`–`0010`). ADR 0010 queues 36
  SM 6.7/6.8/6.9 rules (SER, Cooperative Vectors, Long Vectors, OMM, Mesh
  Nodes) for Phase 3+.
- **C++23 uplift pending.** ADR 0004 locks the C++23 baseline; `CMakeLists.txt`
  still reads `CMAKE_CXX_STANDARD 20` — tracked follow-up before Phase 2
  implementation starts.
- **Phase 2 queued.** ADR 0009: shared-utilities PR + 3 parallel per-category
  packs (math / saturate-redundancy / misc), 24 net-new rules.

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

- **Module decomposition**: current `cli/` + `core/` split is what is
  shipped. The more granular `apps/` + `libs/` + `include/` layout per
  [ADR 0003](docs/decisions/0003-module-decomposition.md) is **Proposed**
  for Phase 1+; it is not silently adopted. Do not restructure the source
  tree without user approval.

- **C++ baseline**: C++23 + selective C++26. Compiler floors: MSVC 19.44+
  (VS 17.14 / Build Tools 14.44), Clang 18+ with libc++ 17+ or libstdc++
  13+, GCC 14+. clang-tidy 19+. NOTE: `CMakeLists.txt` currently sets
  `CMAKE_CXX_STANDARD 20` — uplift is a tracked follow-up before Phase 2
  implementation kicks off. See
  [ADR 0004](docs/decisions/0004-cpp23-baseline.md).

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
  2 Phase 7 — plus 20 DEFERRED and 2 DROPPED candidates. **Proposed**;
  per-phase plans mirror ADR 0009's shared-utilities-PR + parallel-pack
  pattern. See
  [ADR 0011](docs/decisions/0011-candidate-rule-adoption.md).

- **Phase 3 reflection infrastructure**: opaque `reflection.hpp` public
  header + new `Stage::Reflection` + `Rule::on_reflection` virtual + lazy
  per-(SourceId, target-profile) cached `ReflectionEngine` (one global
  Slang `IGlobalSession`, per-worker `ISession` pool); `<slang.h>`
  confined to `core/src/reflection/slang_bridge.cpp`. **Proposed**;
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
  tolerance per ADR 0002. **Proposed**; gates ALL ~45 Phase 4 rules
  across ADR 0007 §Phase 4 / ADR 0010 §Phase 4 / ADR 0011 §Phase 4.
  Does NOT depend on ADR 0012 / Phase 3 — CFG works without
  reflection. Lands as sub-phase 4a (infra PR) → 4b (shared utilities)
  → 4c (5 parallel rule packs). See
  [ADR 0013](docs/decisions/0013-phase-4-control-flow-infrastructure.md).

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

- **Microsoft GSL** for `gsl::span`, `gsl::not_null`, `gsl::narrow_cast`,
  `Expects`/`Ensures`. NOT `gsl::owner` or `gsl::string_span`.

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

- **macOS CI deferred to Phase 5.** Linux + Windows CI is stable. macOS
  Slang/Metal paths have been historically rocky.

- **CMakeLists.txt still sets `CMAKE_CXX_STANDARD 20`.** ADR 0004 locks
  C++23; the build-system edit is a tracked follow-up before Phase 2.

- **`<slang.h>` must never appear in `core/include/hlsl_clippy/`.** CI grep
  enforces this. Same constraint for `tree_sitter/api.h`.

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
to source build by clearing the cache. macOS path not implemented (Phase 5).

---

## Phase status

| Phase | State | Notes |
|---|---|---|
| 0 — First real diagnostic | DONE | `pow-const-squared` end-to-end; smoke tools; CI; 17-shader corpus; 7 ADRs; blog stub |
| 1 — Rule engine + quick-fix | DONE | Suppression; declarative TSQuery; `Rewriter` + `--fix`; `.hlsl-clippy.toml` + `--config`; `redundant-saturate`; `clamp01-to-saturate`; 46/46 tests; 27-shader corpus; 22 expansion fixtures; 3 ADRs |
| 2 — AST-only rule pack | QUEUED | ADR 0009: shared-utilities PR + 3 parallel category packs (math / saturate-redundancy / misc); 24 net-new rules |
| 3 — Reflection-aware | PLANNED | ADR 0007 Phase 3 rules (15) + ADR 0010 Phase 3 rules (23 incl. SER, Cooperative Vectors, Long Vectors, OMM, Mesh Nodes gated) |
| 4 — Control flow + light data flow | PLANNED | ADR 0007 Phase 4 (19) + ADR 0010 Phase 4 (10 incl. SER coherence, uniformity) — 42 rules total |
| 5 — LSP + IDE | PLANNED | JSON-RPC LSP server; VS Code extension; macOS CI added |
| 6 — Launch (v0.5) | PLANNED | CI gate mode; docs site; one blog post per rule; release artifacts per ADR 0005 |
| 7 — IR-level / stretch | PLANNED | Register pressure; redundant samples; packed-math precision; `live-state-across-traceray`; `maybereorderthread-without-payload-shrink` |

---

## ADR index

All 13 ADRs are in MADR 4.0 format under `docs/decisions/`. Each ADR's
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
| [0008](docs/decisions/0008-phase-1-implementation-plan.md) | Phase 1 implementation plan | Proposed |
| [0009](docs/decisions/0009-phase-2-implementation-plan.md) | Phase 2 implementation plan — AST-only rule pack | Proposed |
| [0010](docs/decisions/0010-sm69-rule-expansion.md) | SM 6.9 rule expansion (+36 rules) | Proposed |
| [0011](docs/decisions/0011-candidate-rule-adoption.md) | Candidate rule adoption — underexplored portable surfaces (per-phase plan) | Proposed |
| [0012](docs/decisions/0012-phase-3-reflection-infrastructure.md) | Phase 3 reflection infrastructure — Slang reflection plumbed into RuleContext | Proposed |
| [0013](docs/decisions/0013-phase-4-control-flow-infrastructure.md) | Phase 4 control-flow / data-flow infrastructure — CFG + uniformity oracle | Proposed |

"Proposed" ADRs represent plans that are approved in principle but not yet
fully implemented. "Accepted" ADRs represent shipped decisions. Do not
amend an Accepted ADR to change a decision — add an addendum ADR instead.

---

## File map (top-level)

```
cli/                    hlsl-clippy executable (src/main.cpp)
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
  decisions/            ADRs in MADR 4.0 format (0001–0010)
  rules/                per-rule pages; _template.md; pre-populated catalog
  blog/                 per-rule blog posts (CC-BY-4.0); VitePress scaffold
  architecture.md       high-level pipeline diagram
.github/workflows/      ci.yml, lint.yml, codeql.yml
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
