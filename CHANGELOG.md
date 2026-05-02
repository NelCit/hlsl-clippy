# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

### Changed

### Fixed

### Deprecated

## [1.2.0] — 2026-05-02

**v1.2 — machine-applicable fix conversion sweep (ADR 0019 §"v1.x patch
trajectory" criterion #5).** Builds on the v1.2 foundation in commit
`74097b7` (purity oracle + DXGI format reflection + configurable epsilon
surface) to convert three rules from suggestion-only to machine-applicable,
and to wire two reflection-stage stubs to the new `dxgi_format` field so
they auto-fire when a future Slang surfaces the relevant format
qualifiers. Honest machine-applicable count moves from 42 / 159 (26.4%) to
45 / 159 (28.3%). The 50% target documented in ADR 0019 remains
unreachable without v1.3+ infrastructure (multi-statement rewrites, IEEE
precision, lossy quantisation, attribute trade-offs); this release closes
out the conversions specifically unlocked by the v1.2 foundation.

### Added

- **`RuleContext::config()` / `RuleContext::set_config()`** — new accessor
  + setter on the rule context that exposes the active `Config*` to rules
  that need project-tuned scalar dials. Returns `nullptr` when the lint
  run was started via a non-config-aware overload (legacy callers); rules
  fall back to documented hard-coded defaults in that case.
  `core/include/hlsl_clippy/rule.hpp`, `core/src/lint.cpp`.
- **Tests** — 7 new `[fix]` cases covering the v1.2 conversions
  (machine-applicable on pure operands, suggestion-grade fallback on
  impure operands, project-tuned epsilon plumbing): 825 -> 827 cases /
  2146 -> 2158 assertions on top of the v1.2-foundation baseline.

### Changed

- **`lerp-on-bool-cond`** — was suggestion-only; now machine-applicable
  when both `a` and `b` operands classify as `SideEffectFree` under the
  v1.2 purity oracle. Suggestion-only fallback when either operand is
  impure (the `?:` rewrite drops one of `{a, b}` along each branch, so
  side-effect freedom is required for safety). Fix replacement is
  `cond ? b : a` (or the inverted form `cond ? a : b` for the
  ternary-of-zero-and-one case).
- **`compare-equal-float`** — was suggestion-only with no TextEdit; now
  rewrites `a == b` to `abs((a) - (b)) < <epsilon>` and `a != b` to
  `abs((a) - (b)) >= <epsilon>`, where `<epsilon>` comes from the new
  `Config::compare_epsilon()` (see `[float] compare-epsilon`,
  default 1e-4). Machine-applicable when both operands classify as pure
  (the textual rewrite preserves operand evaluation count). Suggestion-
  only fallback otherwise.
- **`div-without-epsilon`** — was suggestion-only with no TextEdit; now
  wraps the divisor in `max(<epsilon>, <divisor>)`, where `<epsilon>`
  comes from the new `Config::div_epsilon()` (see `[float] div-epsilon`,
  default 1e-6). Machine-applicable when the divisor classifies as pure;
  suggestion-only fallback otherwise.
- **`manual-srgb-conversion`** — was a forward-compatible stub that
  never fired; now probes `ResourceBinding::dxgi_format` for the SRGB
  suffix and emits a suggestion-grade diagnostic at every `pow(...,2.2)`
  call site when at least one bound texture is SRGB-flagged. Today's
  Slang 2026.7.1 ABI does not surface the SRGB qualifier, so the gate
  stays unsatisfied in practice; the rule auto-fires when a future Slang
  surfaces the qualifier with no further code change.
- **`bgra-rgba-swizzle-mismatch`** — was a forward-compatible stub that
  never fired; now probes `ResourceBinding::dxgi_format` for the
  `B8G8R8A8` channel-order suffix and emits a suggestion-grade diagnostic
  anchored at the binding's declaration when at least one bound texture
  is BGRA-flagged. Today's Slang 2026.7.1 ABI does not surface BGRA
  channel order, so the gate stays unsatisfied in practice; the rule
  auto-fires when a future Slang surfaces the order with no further code
  change.

### Skipped

- **`redundant-unorm-snorm-conversion`** — the original rule emits a
  suggestion-grade `x` rewrite at AST stage with no link to the source
  binding. Upgrading to machine-applicable would require pairing the AST
  divide site with the specific resource binding it sampled, which is a
  multi-stage refactor beyond v1.2 scope. The rule keeps its existing
  suggestion-grade behaviour; v1.3+ will revisit if reflection surfaces
  enough information to safely upgrade.

### Fixed

### Deprecated

## [1.1.0] — 2026-05-02

**v1.1 — release-quality maintenance.** Closes the v1.1 ship list
from [ADR 0019](docs/decisions/0019-v1-release-plan.md) §"v1.x patch
trajectory": branch-coverage gate, full FP-rate triage, Marketplace +
downstream-integration metrics polling. No new rules; this release is
infrastructure + governance.

### Added

- **Branch-coverage CI gate** (v1.1, ADR 0019 §"v1.x patch trajectory") —
  the `coverage` job in `.github/workflows/ci.yml` now parses
  `llvm-cov-18 report --show-branch-summary` and fails when line coverage
  drops below 80% or branch coverage drops below 70% on `core/`. The
  job is currently `continue-on-error: true` so the gate stays honest
  while the test suite catches up to the new floor (pre-v1.1 baseline
  was ~62% branch per ADR 0019). Threshold lift to merge-blocking is a
  v1.1.x patch follow-up.
- **`tools/fp-rate-triage.ps1`** (v1.1) — deterministic FP-rate triage
  pass over `tests/corpus/FP_RATES.md`. Maps each firing rule to a list
  of natural-domain corpus prefixes (compute / pixel / raytracing / …);
  classifies firings as TP / FP / MIXED / NEEDS-HUMAN; renders a new
  "Above-budget rules (FP rate > 5%)" section at the top of FP_RATES.md.
  Preserves maintainer-edited rows on re-run (only `TODO` entries are
  rewritten). v1.1 baseline triage finds 1 above-budget rule:
  `vgpr-pressure-warning` (static estimate unreliable on small shaders;
  needs threshold-tuning patch).
- **`tools/adoption-poll.{ps1,sh}`** (v1.1) — polls the VS Code
  Marketplace listing for `nelcit.hlsl-clippy` (installs / rating /
  version) via `vsce show --json` and counts public GitHub repos
  referencing `hlsl-clippy` from a workflow file via `gh search code`.
  Appends one dated row per invocation to `docs/adoption-metrics.md`.
  Suggested cadence: monthly. Captures data only — the maintainer
  reviews the install / downstream thresholds (ADR 0018 §5 #7, #8) at
  each release.
- **`docs/adoption-metrics.md`** — created on first `adoption-poll` run;
  documents the v1.1.x review cadence and thresholds.
- **`tools/README.md`** — inventory of maintainer scripts; called out
  the v1.1 entries (`fp-rate-triage.ps1`, `adoption-poll.{ps1,sh}`)
  alongside the existing build / smoke / release / doc tooling.

### Changed

- **`tests/corpus/FP_RATES.md`** — populated by the v1.1 deterministic
  triage. 39 of 40 firing rules now classify as `TP`; 1 (`vgpr-pressure-warning`)
  classifies as `FP` and lands in the new "Above-budget rules" section
  at the top of the file. Two `clippy::*` infrastructure diagnostics
  flagged `NEEDS-HUMAN` for maintainer review.

### Fixed

### Deprecated

## [1.0.0] — 2026-05-02

**v1.0 — graduated from research preview to stable release.** 190
registered rules, API stability commitment, v1.x maintenance contract.

The pre-v1 / v0.x labels signalled "we reserve the right to break
things." v1.0 freezes the public API: `core/include/hlsl_clippy/*.hpp`
types, CLI flags + output formats, LSP wire protocol (engine
diagnostic codes plus standard LSP). A v1.0 → v1.x bump may not change
the binary shape of those surfaces. Removing or renaming a public type
is a v2.0 break.

See [docs/decisions/0019-v1-release-plan.md](docs/decisions/0019-v1-release-plan.md)
for the per-criterion v1.0 disposition table and the v1.x maintenance
contract (4-week minor cadence, 1-week security/regression patch SLA,
deprecation policy, Slang submodule cadence policy).

### Added

- **`docs/api-stability.md`** — the public-API contract. Tables of
  stable surface (public headers + CLI + LSP), explicitly non-stable
  surface (private headers, dev-shell scripts, exact diagnostic
  message wording), and a regression-reporting note.
- **3 new CI gates**:
  - `api-symbol-diff` — extracts `nm` symbols from `hlsl_clippy_core`
    and uploads as artefact. The `diff` step against the v1.0
    baseline is a v1.0.x patch follow-up.
  - `slang-bump-regression` — nightly cron, bumps `HLSL_CLIPPY_SLANG_VERSION`
    one patch, builds + tests. Catches Slang-side breakages before
    user-visible bumps.
  - `ihv-target-snapshot` — runs `hlsl-clippy lint tests/corpus/`
    once per `[experimental.target]` value. Default-config snapshot
    must contain zero IHV-target-coded diagnostics.
- **`tests/unit/test_ihv_target_snapshot.cpp`** — in-tree complement
  to the CI snapshot job; 4 cases / 12 assertions.
- **`tools/release-audit.{ps1,sh}`** — pre-tag audit. 6 checks: DCO
  trailer on every commit since previous tag, Conventional-Commits
  subject on every commit, CHANGELOG entry exists for the new
  version, version strings synced across `core/src/version.cpp` /
  `vscode-extension/package.json` / CHANGELOG / git tag, ADR index
  consistency, public-header guard.
- **`tools/fp-rate-baseline.ps1`** — runs `hlsl-clippy lint --format=json`
  across `tests/corpus/`, aggregates per-rule firing counts, generates
  `tests/corpus/FP_RATES.md` for human triage. Initial baseline: 39
  rules fire / 211 diagnostics across the 27-shader corpus.
- **`docs/rules/_template.md`** — required `references:` front-matter
  field (≥ 2 entries) for v0.8+ rules. Pre-v0.8 rules grandfathered.
- **`docs/blog/<id>.md`** — per-rule blog post stubs for every rule.
  204 stubs generated; the existing `pow-const-squared.md` full post
  + 8 category-overview launch posts + 1 preface kept untouched.
  Full-length per-rule prose ships incrementally over v1.x; target
  ≥ 80% by v1.6.
- **ADR 0019** — v1.0 release plan. Closes 10 of 12 ADR 0018 §5
  readiness criteria; criteria #7 (Marketplace install count) and
  #8 (downstream-integration count) deferred to v1.1 readiness review.
- **4 machine-applicable fix conversions** (criterion #5 sweep):
  `acos-without-saturate` → `clamp(x, -1.0, 1.0)`,
  `sqrt-of-potentially-negative` → `max(0.0, x)`,
  `select-vs-lerp-of-constant` → `mad(t, K2 - K1, K1)`,
  `ser-coherence-hint-bits-overflow` → clamp bits literal to spec cap.
  Final ratio: 42 / 159 = 26.4% (was 23.9% pre-sweep). The 50% target
  in ADR 0018 §5 #5 is honestly out of reach without infrastructure
  that doesn't exist yet (side-effect-purity oracle, DXGI format
  reflection, configurable epsilons); criterion #5 is **lifted to v1.2**
  alongside that infrastructure (per ADR 0019).

### Changed

- ROADMAP.md — Phase 8 marked DONE; Phase 9 (v1.0 release) added
  IN PROGRESS.
- CLAUDE.md ADR index → 19 ADRs.

### Deprecated

- _(none — v1.0 is a clean baseline; deprecations only land in v1.x.)_

## [0.8.0] — 2026-05-02

**Phase 8 — Research-driven expansion.** 21 new rules (17 LOCKED + 4
DEFERRED per ADR 0018) covering SM 6.10 surfaces (`linalg::Matrix`,
`[GroupSharedLimit]`, cluster-geometry, `getGroupWaveIndex`),
re-classified VRS rules, ray-tracing patterns inspired by Nsight /
RGA, and IHV-experimental rules gated behind a new
`[experimental.target]` config surface. Total registered rule count:
169 → **190**.

This release is the first to land a per-IHV gate. Default builds emit
zero IHV-specific diagnostics; users opt in via `.hlsl-clippy.toml`:

```toml
[experimental]
target = "rdna4"  # or "blackwell" | "xe2"
```

Stub burndown: `numthreads-not-wave-aligned` and `dot4add-opportunity`
were docs-only stubs from earlier ADRs (0007 / 0011); now fully
implemented.

### Added

- **Pack v0.8 — SM 6.10 + stub burndown (8 rules)**:
  - `linalg-matrix-non-optimal-layout` — `linalg::*Mul` calls with
    `MATRIX_LAYOUT_ROW_MAJOR` / `_COLUMN_MAJOR` instead of `OPTIMAL`.
  - `linalg-matrix-element-type-mismatch` — low-precision matrix
    elements with high-precision accumulators (silent widening).
  - `getgroupwaveindex-without-wavesize-attribute` — SM 6.10
    intrinsic without `[WaveSize(N)]`.
  - `groupshared-over-32k-without-attribute` — total groupshared
    > 32 KB without `[GroupSharedLimit(N)]`. Machine-applicable fix.
  - `triangle-object-positions-without-allow-data-access-flag` —
    every call site (project-side BLAS flag invisible from shader).
  - `numthreads-not-wave-aligned` — stub-burndown: total threads
    not a multiple of 32.
  - `dispatchmesh-grid-too-small-for-wave` — `DispatchMesh` constant
    args with product < 32.
  - `dot4add-opportunity` — stub-burndown: 4-component swizzle-
    product chain matches `dot4add_*`.
- **Pack v0.9 — VRS + DXR + Nsight-gap (5 rules)**:
  - `vrs-rate-conflict-with-target` — PS writes `SV_ShadingRate` AND
    a coarse-rate marker is present.
  - `vrs-without-perprimitive-or-screenspace-source` — PS emits
    `SV_ShadingRate` without an upstream VRS source.
  - `ray-flag-force-opaque-with-anyhit` — `TraceRay`
    `RAY_FLAG_FORCE_OPAQUE` in TU defining `[shader("anyhit")]`.
  - `ser-coherence-hint-bits-overflow` — `MaybeReorderThread` bits
    arg above spec cap (16 / 8 for HitObject variant).
  - `sample-use-no-interleave` — `Sample()` result consumed within
    ≤3 statements (Nsight L1-Long-Scoreboard pattern).
- **Pack v0.10 — IHV-experimental, all gated (4 rules)**:
  - `wave64-on-rdna4-compute-misses-dynamic-vgpr` — gated `Rdna4`.
  - `coopvec-fp4-fp6-blackwell-layout` — gated `Blackwell`.
  - `wavesize-32-on-xe2-misses-simd16` — gated `Xe2`
    (suggestion-grade).
  - `cluster-id-without-cluster-geometry-feature-check` — SM 6.10+
    only. Calls without `IsClusteredGeometrySupported()` guard.
- **DEFERRED candidates — implemented defensively (4 rules)**:
  - `oriented-bbox-not-set-on-rdna4` — gated `Rdna4`. Once-per-source
    informational when RT calls are present.
  - `numwaves-anchored-cap` — `[numthreads(...)]` total > 1024
    (defensive for HLSL specs proposal 0054).
  - `reference-data-type-not-supported-pre-sm610` — `<qual> ref
    <type>` patterns under target < SM 6.10.
  - `rga-pressure-bridge-stub` — once-per-source informational on
    RDNA targets noting `tools/rga-bridge` would yield more accurate
    VGPR counts (placeholder for v0.10 infrastructure investment).

### Added (infrastructure)

- **`Rule::experimental_target()` virtual** + **`ExperimentalTarget`
  enum** in `<hlsl_clippy/rule.hpp>`. Rules opt into the gate by
  overriding the virtual; default `None` keeps them always-on.
- **`Config::experimental_target()`** parsed from `[experimental]
  target = ...`. Recognised tokens: `"rdna4"`, `"blackwell"`, `"xe2"`.
  Unrecognised values fall back to `None` with a warning.
- **`core/src/rules/util/sm6_10.{hpp,cpp}`** — shared SM 6.10 helpers:
  `target_is_sm610_or_later()`, `is_linalg_matrix_type()`,
  `parse_groupshared_limit_attribute()`, `expected_wave_size_for_target()`.

### Changed

- ADR 0018 §4 specced 13 of the 21 rules as `Stage::Reflection`. Slang
  2026.7.1's HLSL frontend rejects synthetic test sources containing
  `linalg::Matrix<...>`, payload structs, or `inout ref` syntax (the
  pinned Slang doesn't yet recognise SM 6.10 + reference types). Those
  rules ship as `Stage::Ast` with self-gating on textual source markers.
  The `[experimental.target]` gating works correctly via
  `Rule::experimental_target()` regardless of stage.

## [0.7.0] — 2026-05-02

**Phase 7 — IR-level / stretch rule pack.** 15 new rules covering DXR
ray-tracing patterns, mesh-shader output budgets, packed-math precision,
and register-pressure / memory-coalescing heuristics. Total registered
rule count rises 154 → **169**.

Phase 7 originally specced (ADR 0016) a DXIL bridge built on a vendored
DXC submodule + LLVM bitcode reader. A mid-implementation review (ADR
0017) found that the existing AST + reflection + Phase 4 CFG
infrastructure covers every Phase 7 rule's actual question — the
"IR-level" framing was over-conservative, drafted before Phase 4's CFG
+ uniformity oracle existed. v0.7 ships with **zero new external
dependencies**: no DXC, no spirv-tools, no LLVM. Two new shared
utilities back the rules (`liveness` over the Phase 4 CFG,
`register_pressure_ast` heuristic over reflection types).

If a future rule turns out to genuinely need post-codegen IR access,
ADR 0016's `Stage::Ir` + `<hlsl_clippy/ir.hpp>` are still in force as
the dispatch hook (currently used for stage-gating only); a v0.8+
follow-up ADR can re-introduce the bridge cleanly.

### Added

- **Pack DXR (5 rules)**:
  - `oversized-ray-payload` — payload struct > 32 bytes (ray-stack
    pressure on every IHV).
  - `missing-accept-first-hit` — `TraceRay(...)` flag-arg lacks
    `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` inside shadow / occlusion
    named functions.
  - `recursion-depth-not-declared` — raygen entry calls `TraceRay`
    without `MaxRecursionDepth` / `[shader_recursion_depth]`.
  - `live-state-across-traceray` — > 2 locals live across a `TraceRay`
    call site (will spill to the ray stack).
  - `maybereorderthread-without-payload-shrink` — same shape, SM 6.9
    SER reorder anchored at `MaybeReorderThread` call sites.
- **Pack Mesh (2 rules)**:
  - `meshlet-vertex-count-bad` — `out vertices arr[N]` with N > 128 OR
    N % 32 != 0 (RDNA optimal / NVIDIA wave-aligned).
  - `output-count-overrun` — `SetMeshOutputCounts(v, p)` literal
    exceeds `[outputtopology(...)]` declared ceilings.
- **Pack Precision/Packing (3 rules)**:
  - `min16float-opportunity` — `(float)<half> * <16-bit literal>`
    chains where reverting to half saves ALU throughput.
  - `unpack-then-repack` — `pack(unpack(x))` round-trips on the same
    lanes (8 outer / inner pack-pair signatures recognised).
  - `manual-f32tof16` — bit-twiddling lowering of `f32tof16`
    (`asuint(x) >> 13` etc.).
- **Pack Pressure/Memory (5 rules)**:
  - `vgpr-pressure-warning` — per-block live-AST-value × bit-width
    estimate exceeds `LintOptions::vgpr_pressure_threshold` (default 64).
  - `scratch-from-dynamic-indexing` — local array indexed by a
    non-literal expression (falls back to scratch on most IHVs).
  - `redundant-texture-sample` — identical `Sample(s, uv)` calls
    within one basic block.
  - `groupshared-when-registers-suffice` — `groupshared T arr[N]`
    with N ≤ 8 + thread-id-like access pattern.
  - `buffer-load-width-vs-cache-line` — 2-4 scalar `.Load` calls
    within a 16-byte offset window (coalesces to `Load4`).
- **Liveness analysis** (`core/src/rules/util/liveness.{hpp,cpp}`) —
  backward dataflow fixed-point iteration over the Phase 4
  `ControlFlowInfo`. Public API: `compute_liveness(cfg)`,
  `live_in_at(block)`, `live_out_at(block)`. Used by 3 of the 5 DXR
  rules and by the SM 6.9 reorder rule.
- **Register-pressure heuristic**
  (`core/src/rules/util/register_pressure_ast.{hpp,cpp}`) — per-block
  estimate `Σ (live_value_bit_width / 32)`, with bit-width from
  reflection cbuffer/parameter types or declaration-site lexeme scan
  fallback (`min16float`, `half`, `double`, `uint16_t`, vector / matrix
  shapes). Used by `vgpr-pressure-warning` and
  `groupshared-when-registers-suffice`.

### Changed

- ADR 0016 sub-phase 7a.2-step2 (DXC submodule + DXIL parser) and 7b's
  IR-based shared utilities — **superseded by ADR 0017**. ADR 0016's
  shipped pieces (`Stage::Ir` enum, `<hlsl_clippy/ir.hpp>`, metadata-
  only `IrEngine` from 7a.2-step1, `LintOptions::enable_ir`) stay in
  force as a stage-gating dispatch hook; the v0.7 rules that use
  `Stage::Ir` use it for stage gating only.

## [0.6.8] — 2026-05-02

Editor experience + rule-pack maintenance release. Two LSP plumbing bugs
that made VS Code feel broken are gone, fix-all now actually works, and
10 rules previously emitting Fix-less diagnostics now carry a TextEdit.

### Fixed
- **LSP `diagnosticProvider` capability**: the server advertised LSP 3.17
  pull diagnostics it never implemented, so vscode-languageclient v9
  spammed `Document pull failed` (-32601 method-not-found) for every
  open `.hlsl` file. We push diagnostics via `publishDiagnostics`;
  removed the bogus capability advertisement and added a regression
  test that asserts the pull method stays unimplemented.
- **LSP min-precision Slang failures surfaced as Error**: any file
  using DXC's minimum-precision types (`min16float` / `min16uint` /
  `min16int`) — which Slang's HLSL frontend doesn't accept — emitted a
  red `clippy::reflection` Error in the IDE even though AST-only rules
  ran cleanly. Now demoted to `Severity::Note` (LSP Information) when
  the failure is *only* min-precision-related, with a message pointing
  the user at the spec'd 16-bit type alternatives. Real Slang errors
  (syntax errors, user-defined undefined identifiers) keep
  `Severity::Error`.
- **VS Code "Fix All in Document"**: the `ClippyFixAllProvider` walked
  diagnostics one at a time, calling `executeCodeActionProvider`
  recursively per diagnostic; this hit the LSP N times, returned fixes
  for *every* overlapping diagnostic on each pass, and raced the outer
  CancellationToken on large files. Replaced with a single
  `gatherFixAllEdit(document)` helper that does one full-document
  QuickFix gather, filters aux suppress/open-docs actions, and merges
  every TextEdit into one WorkspaceEdit. The
  `hlslClippy.fixAllInDocument` command and the
  `source.fixAll.hlslClippy` provider both call it.

### Added
- **TextEdit fixes on 10 previously fix-less rules**, after a full
  154-rule audit. Each rule's fix is single-span, side-effect-aware,
  and gated on operand simplicity for the `machine_applicable` flag:
  - `pow-const-squared` — `pow(x, N.0)` → repeated multiplication
    (matches `pow-to-mul` on overlapping exponents; covers exponent 5).
  - `samplegrad-with-constant-grads` — `<tex>.SampleGrad(s, uv, 0, 0)`
    → `<tex>.SampleLevel(s, uv, 0.0)`.
  - `coherence-hint-redundant-bits` — clamp `hintBits` literal to the
    SER spec ceiling (32).
  - `gather-channel-narrowing` — `Gather(...).<ch>` →
    `Gather<Channel>(...).r` (machine-applicable only on `.r`/`.x`).
  - `descriptor-heap-no-non-uniform-marker` — wrap heap index in
    `NonUniformResourceIndex(...)`.
  - `flatten-on-uniform-branch` — `[flatten]` → `[branch]`.
  - `missing-ray-flag-cull-non-opaque` — OR
    `RAY_FLAG_CULL_NON_OPAQUE` into the existing flag expression.
  - `quadany-replaceable-with-derivative-uniform-branch` — unwrap
    `QuadAny(cond)` to bare `cond` (machine-applicable; non-duplicating).
  - `non-uniform-resource-index` — wrap captured index in
    `NonUniformResourceIndex(...)`.
  - `omm-rayquery-force-2state-without-allow-flag` — append
    `| RAY_FLAG_ALLOW_OPACITY_MICROMAPS` to the `RayQuery<...>` flag
    template arg.
- **LSP regression test suite** for the fix-all path: full-pipeline
  `didOpen` → `publishDiagnostics` carries non-empty diagnostics on
  fixable buffers, full-document `textDocument/codeAction` returns a
  quickfix with populated `edit.changes[uri]`, and
  `textDocument/diagnostic` (pull) returns method-not-found.

### Changed
- **5 rule docs `applicability:` tags realigned to reality** (no code
  change beyond what shipped above):
  - `unused-cbuffer-field`: machine-applicable → suggestion. Deleting
    a cbuffer field shifts every subsequent field's offset and shrinks
    the cbuffer's declared size; CPU-side struct mirrors and aliased
    bindings would silently misalign.
  - `dead-store-sv-target`: machine-applicable → suggestion.
    Declaration-vs-assignment ambiguity, and the RHS may have
    observable side effects (UAV writes, atomics) that the structural
    detector cannot see through.
  - `gather-cmp-vs-manual-pcf`: machine-applicable → suggestion.
    Multi-statement rewrite that needs invented blend-variable
    identifiers and depends on a project-specific texel-size cbuffer
    field; the cluster detector also doesn't prove the input is
    actually a 2×2 axis-aligned kernel.
  - `non-uniform-resource-index`: none → suggestion (Fix shipped above).
  - `omm-rayquery-force-2state-without-allow-flag`: none → suggestion
    (Fix shipped above).

## [0.6.7] — 2026-05-02

**Critical hotfix**: v0.6.6's .vsix activation crashed in VS Code with
`Cannot find module 'minimatch'`. The DLL bundling and binary stdio
fixes from v0.6.1 / v0.6.6 were both correct, but the activation
crash happens before either is reached -- VS Code can't even load the
extension's `extension.js` because the `vscode-languageclient` require
chain hits transitive dependencies that the `.vscodeignore`
allow-list excluded.

### Fixed
- **`vscode-extension/.vscodeignore`**: add the three missing prod
  transitives that `vscode-languageclient` v9 pulls in --
  `minimatch`, `brace-expansion`, `balanced-match`. Removed the dead
  `lru-cache` and `yallist` allow-list entries (no longer in the
  prod tree at vscode-languageclient v9). Documented the layout +
  pointer to the new CI guard.

### Added
- **`.github/workflows/lint.yml` `vsix-activation-check` step**: in
  the `vscode-extension-tsc` job, now also runs `vsce package`
  locally, unpacks the .vsix, and diffs the bundled `node_modules/`
  tree against `npm list --production --all`. Any missing prod
  transitive fails the lint job at PR time -- catches exactly the
  v0.6.7 failure mode (and the same class of bug for any future
  vscode-languageclient transitive shuffle).

## [0.6.6] — 2026-05-02

**Critical hotfix**: the LSP server has been broken on Windows since
v0.5.0. `lsp/src/main.cpp` left stdin/stdout in text mode; Windows
silently translates `\r\n` <-> `\n` in text-mode I/O, corrupting the
LSP base-protocol framing in both directions. The framing parser
searched for `\r\n\r\n` but Windows had stripped the `\r`, so the
header terminator never matched -- the server hung forever waiting
for a header that had already arrived. Symptom: VS Code's Problems
panel stayed empty; no error logged anywhere.

Verified end-to-end: `tools/smoke-lsp.js` drives the LSP via JSON-RPC
stdio, sends `initialize` + `didOpen` for `tests/fixtures/phase2/math.hlsl`,
and now receives 23 diagnostics back (`lerp-extremes`, `mul-identity`,
`sin-cos-pair`, `manual-reflect`, `manual-step`, ...). Pre-fix: 0
frames, server hung.

### Fixed
- **`lsp/src/main.cpp`**: call `_setmode(_fileno(stdin), _O_BINARY)`
  + `_setmode(_fileno(stdout), _O_BINARY)` on Windows. Includes
  `<fcntl.h>` and `<io.h>` for the API. Removed the wrong
  "leave it text-mode" comment.

### Added
- **`tools/smoke-lsp.js`** -- standalone Node.js smoke test that
  spawns `hlsl-clippy-lsp.exe`, drives a real JSON-RPC handshake
  through stdio, and asserts at least one `publishDiagnostics`
  frame is produced. Catches both LSP startup failure modes:
  missing DLLs (server crashes before handshake) and text-mode
  CRLF mangling (parser hangs forever). Fast (~3 s end-to-end);
  documented in `vscode-extension/DEVELOPMENT.md` as the cheapest
  pre-tag verification.

## [0.6.5] — 2026-05-02

Hotfix release: v0.6.4's .vsix never published to the Marketplace
because the strict-TS compile step in `release-vscode.yml` rejected
`provideCodeActions(document, range, context)` -- the `document`
parameter was unused (only `range` and `context` were read) and
`tsconfig.json` has `noUnusedParameters: true`. The CLI release in
v0.6.4 succeeded, but every Marketplace user is still on v0.6.3.

### Fixed
- **`vscode-extension/src/extension.ts`**:
  `ClippyAuxCodeActionProvider.provideCodeActions` -- prefix the
  unused first parameter with `_` (`_document`) so TypeScript's
  `noUnusedParameters` strict rule passes. The body never read the
  document because we operate on `context.diagnostics` + `range`.

### Added
- **`.github/workflows/lint.yml`**: new `vscode-extension-tsc` job
  runs `npx tsc -p .` against `vscode-extension/` on every push +
  PR. Catches strict-TS errors (`noUnusedParameters`,
  `noUnusedLocals`, `noImplicitAny`, etc.) BEFORE the release
  workflow tries to package the .vsix at tag time. Adds ~30s to
  the lint pipeline; well worth it -- the v0.6.4 hotfix would have
  been caught at PR time.

## [0.6.4] — 2026-05-02

VS Code extension UX wave #3: inline diagnostic decorations
(Error Lens style), `source.fixAll.hlslClippy` for auto-fix-on-save,
status-bar visibility toggle, and an explicit
"Fix All in Document" command.

### Added
- **Inline diagnostic decorations** (opt-in via
  `hlslClippy.inlineDiagnostics` setting). Renders the diagnostic
  message at the end of the offending line in a dim italic colour
  themed to severity. Three modes: `off` (default), `errors-only`,
  `all`. Only one inline message per line (highest-priority
  diagnostic wins) so dense files stay readable.
- **`source.fixAll.hlslClippy`** code action kind. Apply every
  machine-applicable fix in the active document at once, optionally
  on save:
  ```jsonc
  // settings.json
  "editor.codeActionsOnSave": {
    "source.fixAll.hlslClippy": "always"
  }
  ```
- **`HLSL Clippy: Fix All in Document`** command (also right-click
  menu + status-bar quick-pick) for explicit one-shot invocation.
- **`hlslClippy.showStatusBar`** boolean setting -- hides the
  status-bar badge for users with crowded status bars (commands and
  hotkeys still work).

### Changed
- Status-bar `renderStatus()` re-renders on
  `onDidChangeConfiguration` so toggling
  `hlslClippy.showStatusBar` / `inlineDiagnostics` takes effect
  immediately without a window reload.

## [0.6.3] — 2026-05-02

VS Code extension UX wave: inline right-click commands (no more
submenu nesting), severity-split status bar with click-to-quick-pick,
client-side code actions for suppress-line / suppress-file /
open-docs, "Show All Rules" webview, plus the
`vscode.workspace.save()` API-version compatibility fix that was
masking the v0.6.1 / v0.6.2 "command not found" report.

### Fixed
- **`HLSL Clippy: Re-lint Active Document` no longer throws on VS
  Code 1.85.** v0.6.1 used `vscode.workspace.save()` which is a 1.86+
  API; on 1.85 the call was a TypeError. Replaced with
  `vscode.commands.executeCommand("workbench.action.files.save")`
  which has been stable since 1.0 and operates on the active editor.

### Added
- **Editor right-click menu**: the four user-facing commands now
  appear directly inline (no submenu nesting) when right-clicking
  inside an HLSL file. Order: Open Rule Docs → Suppress for Line →
  Suppress for File → Re-lint → Show All Rules → Show Output.
- **Code actions** (`Ctrl+.` / lightbulb on any HLSL Clippy
  diagnostic):
  - `HLSL Clippy: suppress '<rule>' for this line` -- inserts
    `// hlsl-clippy: allow(rule-id)` at the end of the offending
    line. If a comment already exists, extends its rule list.
  - `HLSL Clippy: suppress '<rule>' for entire file` -- inserts
    (or extends) a top-of-file `// hlsl-clippy: allow(rule-id)`.
  - `HLSL Clippy: open '<rule>' docs` -- opens the per-rule docs
    page on github.com.
  These actions sit alongside the LSP server's existing quick-fixes;
  one diagnostic with a machine-applicable fix now offers four
  lightbulb actions: the fix + the three above.
- **Status-bar severity split**: `$(check) HLSL Clippy $(error) 2
  $(warning) 5 $(info) 1` instead of one flat count. Severity tiers
  with zero count are omitted to save real estate.
- **Status-bar click → quick-pick** (`HLSL Clippy: Quick Actions...`)
  lists every extension command with icons + descriptions. Replaces
  the click-jumps-to-output behaviour from v0.6.1 / v0.6.2; output
  is still on the menu, just not the only option.
- **`HLSL Clippy: Show All Rules`** -- opens a webview side panel
  listing every rule category with a deep link to its docs section.
- **`HLSL Clippy: Open Welcome Walkthrough`** -- re-opens the
  first-install guided tour for users who closed the Welcome tab.
- **Suppress-line / Suppress-file / Quick-Actions / Walkthrough**
  commands also appear in the Command Palette.

## [0.6.2] — 2026-05-02

VS Code extension follow-up to v0.6.1: surface the four extension
commands as a right-click submenu on HLSL files (Re-lint Document,
Open Rule Docs, Show Output, Restart Server) and bind the two
most-used commands to keyboard shortcuts. Closes the
"right-clicking does nothing extension-specific" gap reported on
v0.6.1.

### Added
- **Editor right-click submenu** — right-click anywhere inside an
  HLSL file shows an `HLSL Clippy` submenu with the four extension
  commands. Scoped via `editorLangId == hlsl` so it doesn't pollute
  context menus in other languages.
- **Default keybindings** (HLSL files only):
  - `Ctrl+Alt+L` (`Cmd+Alt+L` on macOS) → `HLSL Clippy: Re-lint
    Active Document`.
  - `Ctrl+Alt+D` (`Cmd+Alt+D` on macOS) → `HLSL Clippy: Open Rule
    Docs` (uses the diagnostic at the cursor; falls back to a
    friendly message if there's none).
- **Command Palette gating** — `HLSL Clippy: Re-lint Active
  Document` and `HLSL Clippy: Open Rule Docs` are filtered out of
  the palette unless the active editor is HLSL, so non-HLSL editors
  don't see commands that would no-op anyway.
- README: new "Commands" table column documents the keybindings;
  bottom of the section documents the right-click submenu.

## [0.6.1] — 2026-05-02

VS Code extension UX patch release. v0.6.0's `.vsix` shipped
`hlsl-clippy-lsp.exe` without its 7 required Slang runtime DLLs, so
the LSP subprocess crashed on Windows before the JSON-RPC handshake
and users saw an empty Problems panel with no error feedback. This
release fixes the bundling, makes activation status visible, and adds
a guided walkthrough for first-time users.

### Fixed
- **`.vsix` bundling: ship the full Slang runtime alongside
  `hlsl-clippy-lsp[.exe]`.** `release-vscode.yml`'s "Stage LSP binary"
  step copied only the EXE; on Windows that meant 7 missing DLLs
  (`slang.dll`, `gfx.dll`, `slang-compiler.dll`,
  `slang-glsl-module.dll`, `slang-glslang.dll`, `slang-llvm.dll`,
  `slang-rt.dll`). The fixed step copies every sibling `.dll` (Windows)
  / `.so*` / `.dylib` (POSIX) the cmake POST_BUILD helper deployed
  next to the EXE in `build/lsp/`. `.vsix` size grows from ~5 MB to
  ~150 MB on Windows but the LSP now actually starts.

### Added
- **Status-bar indicator.** Bottom-right badge shows live LSP health:
  `$(check) HLSL Clippy <count>` when the server is running (with
  diagnostic count for the active document), `$(sync~spin) HLSL Clippy`
  while starting, `$(error) HLSL Clippy` when activation failed (red
  background, click to open the Output channel).
- **VS Code Walkthrough** (`HLSL Clippy: Get started`) — shows on
  first install via Welcome → Walkthroughs. Five steps: open file →
  check status → trigger a rule → apply a quick-fix → configure via
  `.hlsl-clippy.toml`. Each step has command links for one-click
  navigation.
- **`HLSL Clippy: Re-lint Active Document`** command — forces a
  re-lint via a save round-trip. Useful after editing
  `.hlsl-clippy.toml` or toggling settings, when you want the new
  behaviour without typing.
- **`HLSL Clippy: Open Rule Docs`** command — opens the per-rule docs
  page on github.com for the diagnostic at the cursor (or any rule
  ID passed as the argument). Falls back to a friendly message when
  the cursor isn't on a diagnostic.
- **README "How it works in 30 seconds"** + **Troubleshooting**
  section. The Marketplace listing now opens with a five-bullet
  walkthrough of expected behaviour and a three-step diagnostic for
  when something goes wrong.

## [0.6.0] — 2026-05-02

The v0.6 hardening release. Closes the entire post-launch backlog
identified during the v0.5 audits: ~1500 LOC of rule-engine
duplication factored away, lint pipeline 3× faster on CI, two new
runtime perf wins in core + LSP, full coverage gate, nightly bench
with delta-posting, every test green for the first time on Windows
clang-cl, and a documentation refresh that retires every "pre-v0"
banner in `docs/rules/`.

### Added
- Coverage gate: new `coverage` job in `ci.yml` runs Linux Clang 18
  with `-fprofile-instr-generate -fcoverage-mapping`, merges the
  per-process profraw via `llvm-profdata-18`, exports lcov via
  `llvm-cov-18`, and uploads to Codecov via the v5.4.0 action.
  Threshold-enforcement deferred until baseline data lands.
- Nightly bench harness: new `bench.yml` runs on `cron: '17 2 * * *'`
  across Linux + Windows + macOS, RelWithDebInfo build, 200 samples
  per benchmark, Catch2 XML reporter uploaded as a 90-day artifact.
- Bench-history delta-posting: `tools/bench-diff.py` parses two
  Catch2 XML reports (current + previous nightly), renders a sorted
  markdown table of per-benchmark mean deltas (largest |Δ%| first),
  and posts to `$GITHUB_STEP_SUMMARY`. Soft thresholds (10 % yellow,
  25 % red) tolerate GHA-runner noise.
- `.gitattributes`: hard-pin LF on `tests/golden/snapshots/*.json`,
  `tests/golden/fixtures/*.hlsl`, and `*.sh`; CRLF on `*.ps1`. Stops
  fresh Windows checkouts (with `core.autocrlf=true`) from breaking
  the golden harness.
- LSP: new `hlsl_clippy_lsp_lib` STATIC library factor — both the
  server exe and the unit tests link the same 8 LSP TUs once.

### Changed
- Factor `node_kind`, `node_text`, `is_id_char` into shared
  `core/src/rules/util/ast_helpers.{hpp,cpp}`. These three tree-sitter
  helper functions were previously copy-pasted into every rule TU's
  anonymous namespace; with 95+ rule files the duplicates dominated
  post-PCH compile time and any tweak (e.g. an out-of-range guard)
  had to be applied 95 times. New header centralises the canonical
  definition under `hlsl_clippy::rules::util`. Net: 119 rule files
  modified, **1422 LOC removed** (1886 deletions vs 464 insertions).
- Parallelise `clang-tidy` via `run-clang-tidy-18 -j$(nproc)`. Lint
  workflow drops from ~30 min serial to <11 min parallel on
  `ubuntu-latest`. `HeaderFilterRegex` extended from `(cli|core|src)`
  to `(cli|core|lsp|src)` so LSP server headers get tidied too.
- `.clang-tidy`: broaden suppressions for clang-tidy 18 noise (~50
  check classes that fire either as house-style conflicts or
  codebase-pattern escapes — recursion in AST walkers, cognitive
  complexity in rule `scan` functions). Add `EnumConstantCase:
  CamelCase` so the public enum surface stops triggering identifier-
  naming errors.
- Snapshot harness in `tests/unit/test_golden_snapshots.cpp`: filter
  `clippy::*` infrastructure diagnostics (their messages embed
  absolute filesystem paths that vary per machine), extend the sort
  key from (line, col, rule) to (line, col, rule, message) for
  stable tie-breaking, and strip `\r` defensively from both expected
  + actual before comparison.
- 186 rule documentation pages refreshed: 134 stale "pre-v0 — rule
  scheduled for Phase N" banners → "shipped (Phase N)"; 183
  "Companion blog post: _not yet published_" placeholders → links to
  the per-category overview blog posts (per-rule for
  `pow-const-squared`). 154 `since-version:` frontmatter values
  updated from the original phase-plan placeholders to the canonical
  `v0.5.0` launch tag.

### Performance
- CFG engine reuses the parsed tree-sitter tree instead of re-parsing.
  `lint.cpp` already parses every source once for the AST stage; the
  Phase 4 control-flow stage was calling `parser::parse(...)` a second
  time inside `CfgEngine::build`. New `CfgEngine::build_with_tree`
  overload accepts the already-parsed `::TSNode root` + bytes; the
  orchestrator now hands them across, dropping the second parse.
  Estimated saving on the public corpus: 5–15 % of total lint time
  per source with CFG-stage rules enabled.
- LSP serves `textDocument/hover` and `textDocument/codeAction` from
  the cached `OpenDocument::latest_diagnostics`. Previously every
  hover or code-action request triggered a fresh `lint()` call —
  parser + AST walk + reflection + CFG. Hover requests fire at typing
  cadence (once per cursor move) so re-linting on each one was a
  measurable idle-CPU drain. Diagnostics already lived on the
  document via `lint_and_publish`; now they're reused.

### Fixed
- All 4 `STATUS_STACK_BUFFER_OVERRUN` golden-snapshot crashes
  (`tests/KNOWN_FAILURES.md` — phase2-misc, phase3-bindings,
  phase4-atomics, phase4-control-flow) resolved. The "crashes" were
  Catch2 `FAIL()` exceptions tripping `/GS` stack-canary checks on
  snapshot mismatches; root causes were absolute-path leakage from
  Slang reflection, non-deterministic sort tie-break, and
  fixture/Slang-version drift. **672 / 672 tests now pass on
  Windows clang-cl + libstdc++** (was 662 / 672 at v0.5.6).
- Slang threadgroup interop: swap C-style array
  (`SlangUInt thread_group[3]`) for `std::array<SlangUInt, 3>` at
  the `getComputeThreadGroupSize` callsite, passing `.data()` to
  preserve out-pointer interop without tripping
  `cppcoreguidelines-avoid-c-arrays`.
- Rename `prime` → `k_prime` in two FNV-1a fingerprint helpers
  (`core/src/reflection/engine.cpp`,
  `core/src/control_flow/engine.cpp`) so they obey the project's
  `ConstexprVariablePrefix: 'k_'` convention.
- `docs/rules/index.md` + `docs/README.md`: switch ADR cross-links
  from VitePress-style relative paths (which produced "dead link"
  errors during the Docs workflow build) to the absolute GitHub-URL
  form the launch blog posts already use.

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

[1.1.0]: https://github.com/NelCit/hlsl-clippy/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/NelCit/hlsl-clippy/compare/v0.8.0...v1.0.0
[0.8.0]: https://github.com/NelCit/hlsl-clippy/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.8...v0.7.0
[0.6.8]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.7...v0.6.8
[0.6.7]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.6...v0.6.7
[0.6.6]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.5...v0.6.6
[0.6.5]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.4...v0.6.5
[0.6.4]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.3...v0.6.4
[0.6.3]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.2...v0.6.3
[0.6.2]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.1...v0.6.2
[0.6.1]: https://github.com/NelCit/hlsl-clippy/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.6...v0.6.0
[0.5.6]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.5...v0.5.6
[0.5.5]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.4...v0.5.5
[0.5.4]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.3...v0.5.4
[0.5.3]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.2...v0.5.3
[0.5.2]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/NelCit/hlsl-clippy/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/NelCit/hlsl-clippy/releases/tag/v0.5.0
