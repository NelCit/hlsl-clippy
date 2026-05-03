---
status: Accepted
date: 2026-05-02
decision-makers: maintainers
consulted: ADR 0007, ADR 0010, ADR 0011, ADR 0013, ADR 0016
supersedes-section: ADR 0016 §"Considered Options" / §"Decision Outcome" / §"Implementation sub-phases"
tags: [phase-7, revision, addendum, scope-correction]
---

# Phase 7 revised: no DXIL/SPIR-V bridge — AST + reflection + Phase 4 CFG cover the surface

## Context

ADR 0016 was Accepted on 2026-05-02 with a DXIL-only IR bridge as the
foundation of Phase 7. Sub-phase 7a.1 shipped (`Stage::Ir` enum, opaque
`<shader_clippy/ir.hpp>`, skeleton `IrEngine`). Sub-phase 7a.2-step1
shipped (engine returns metadata-only `IrInfo` from reflection).
Sub-phase 7a.2-step2 was scoped to the DXC submodule + DXIL parser.

A maintainer review mid-implementation surfaced the question: **does
Phase 7 actually need a lowered-IR layer at all?** ADR 0016 assumed yes.
Re-examining the 15 candidate rules under the lens of the *current*
infrastructure (which gained Phase 4 CFG + uniformity oracle in ADR 0013
since the Phase 7 rule set was originally drafted in ADR 0007), the
answer is no.

## Per-rule audit — actual stage requirement

| Rule | What it actually checks | Stage available today |
|---|---|---|
| `oversized-ray-payload` | payload struct size in bytes | Reflection |
| `missing-accept-first-hit` | walk `TraceRay(...)`, inspect flag arg literal | AST |
| `recursion-depth-not-declared` | raygen entry's `[shader_recursion_depth]` attribute / Slang's max-trace-recursion-depth metadata | AST + Reflection |
| `meshlet-vertex-count-bad` | `[outputtopology]` + declared output count | AST + Reflection |
| `output-count-overrun` | track `SetMeshOutputCounts(...)` calls against `[outputtopology(...)]` | AST + ControlFlow |
| `redundant-texture-sample` | identical `Sample(s,uv)` calls within one basic block | AST + ControlFlow |
| `live-state-across-traceray` | locals defined-before / read-after a `TraceRay` call | AST + ControlFlow |
| `maybereorderthread-without-payload-shrink` | same liveness, anchored at the `MaybeReorderThread` call site | AST + ControlFlow |
| `min16float-opportunity` | type analysis on ALU chains where intermediate precision suffices | AST + Reflection |
| `unpack-then-repack` | pack -> unpack -> pack pattern across same lanes | AST |
| `manual-f32tof16` | bit-manipulation patterns that compile to `f32tof16` | AST |
| `groupshared-when-registers-suffice` | per-thread temporary array of size <= N + access pattern | AST + Reflection |
| `buffer-load-width-vs-cache-line` | scalar `Load`s in a wave that aggregate to a Load4 | AST + ControlFlow (heuristic) |
| `scratch-from-dynamic-indexing` | dynamic index into local array | AST |
| `vgpr-pressure-warning` | per-block live AST-level value count, weighted by reflection-typed bit width | AST + ControlFlow (heuristic) |

**Every Phase 7 rule is answerable from infrastructure that exists
today.** The "IR-level" framing in ADR 0007 was overly conservative
because the Phase 4 CFG + uniformity oracle (ADR 0013) did not yet
exist at the time the rule set was drafted.

## Decision

Cancel ADR 0016's sub-phase 7a.2-step2, sub-phase 7b shared utilities
that depended on parsed DXIL (`liveness` over IR, `register_pressure`
over IR), and the entire DXC dependency chain. Replace with:

### What stays from ADR 0016

* `Stage::Ir` enum — kept as a stage-gating hook. Rules that only need
  to fire on entry points of a particular stage (e.g.
  `oversized-ray-payload` -> "raygeneration") use it.
* `Rule::on_ir(tree, ir, ctx)` virtual — kept; receives the
  metadata-only `IrInfo` already produced by 7a.2-step1.
* `<shader_clippy/ir.hpp>` opaque types (`IrInfo`, `IrFunction`,
  `IrFunctionId`, etc.) — kept; `IrFunction::blocks` will stay empty.
  Future ADR can populate it if a real consumer drives demand.
* `IrEngine` reflection-driven implementation (7a.2-step1) — kept.
* `LintOptions::enable_ir` + `vgpr_pressure_threshold` — kept; the
  threshold is now consumed by the AST-level pressure heuristic.
* CMake option `SHADER_CLIPPY_ENABLE_IR` — kept; same semantics.

### What's removed from ADR 0016

* Sub-phase 7a.2-step2 (DXC submodule + `cmake/UseDxc.cmake` +
  `tools/fetch-dxc.{sh,ps1}` + `dxil_bridge.cpp` + `debug_info.cpp`).
  **Cancelled.**
* Sub-phase 7b's `liveness.{hpp,cpp}` over IR. **Replaced** with a
  liveness extension over the Phase 4 CFG, living under
  `core/src/control_flow/liveness.{hpp,cpp}`.
* Sub-phase 7b's `register_pressure.{hpp,cpp}` over IR. **Replaced**
  with an AST + reflection heuristic under
  `core/src/rules/util/register_pressure_ast.{hpp,cpp}`.
* DXC dep, prebuilt cache, CI grep clause for DXC headers. None of
  this is needed.

### What's new in this ADR

* **Sub-phase 7b.1** (sequential, lands first): liveness analysis
  extension to Phase 4 CFG. Backward dataflow over the existing
  `ControlFlowInfo` storage. ~150 LOC + tests. No new public types
  in `<shader_clippy/control_flow.hpp>`; the liveness API lives in a
  shared utility header `core/src/rules/util/liveness.{hpp,cpp}`
  that consumes `ControlFlowInfo` and produces per-CFG-node
  live-in / live-out sets. Rules that need liveness include
  `<rules/util/liveness.hpp>` and call
  `compute_liveness(cfg) -> LivenessInfo`.

* **Sub-phase 7b.2** (sequential, lands second): register-pressure
  heuristic. Counts AST-level live values per CFG block, weighted
  by reflection-typed bit width. Lives under
  `core/src/rules/util/register_pressure_ast.{hpp,cpp}`. Reads the
  existing `LintOptions::vgpr_pressure_threshold`. Documented as
  a heuristic; v0.8+ refinement (linkable RGA, accurate per-arch
  counts) is a separate ADR if real demand surfaces.

* **Sub-phase 7c** (parallel after 7b): four rule packs,
  worktree-isolated, mergeable independently. The pack split
  matches ADR 0016's plan with the rule->stage assignment from
  the audit table above:
  - **Pack DXR (5 rules)**: `oversized-ray-payload` (Reflection),
    `missing-accept-first-hit` (AST), `recursion-depth-not-declared`
    (AST + Reflection), `live-state-across-traceray` (ControlFlow),
    `maybereorderthread-without-payload-shrink` (ControlFlow).
  - **Pack Mesh (2 rules)**: `meshlet-vertex-count-bad` (AST +
    Reflection), `output-count-overrun` (ControlFlow).
  - **Pack Precision/Packing (3 rules)**: `min16float-opportunity`
    (AST + Reflection), `unpack-then-repack` (AST),
    `manual-f32tof16` (AST).
  - **Pack Pressure/Memory (5 rules)**: `vgpr-pressure-warning`
    (ControlFlow + register-pressure utility),
    `scratch-from-dynamic-indexing` (AST),
    `redundant-texture-sample` (ControlFlow),
    `groupshared-when-registers-suffice` (AST + Reflection +
    register-pressure utility),
    `buffer-load-width-vs-cache-line` (ControlFlow).

  All 15 rules ship at warn severity per ADR 0016's
  "Best-effort precision" decision driver.

* **v0.7.0 release** when all four packs merge and CI is green.
  Phase 7 closes; ADR 0016 + this ADR + ADR 0011's Phase 7 LOCKED
  rules are all retired. Roadmap moves to "v0.8+ open-ended".

## Trade-offs we accept by skipping the lowered-IR layer

* **`vgpr-pressure-warning` is heuristic at source level** — it
  over-counts compared to what the actual DXIL register allocator
  emits, because:
  - We can't see compiler-introduced spills.
  - We can't see compiler dead-code elimination (a value live in
    AST may not be live in DXIL).
  - We approximate VGPR count as `live AST values * type-bit-width / 32`.
  Mitigation: warn severity + tunable
  `LintOptions::vgpr_pressure_threshold` (default 64, ~2x a wave's
  full-width VGPRs) + inline suppression per ADR 0008.
* **`live-state-across-traceray` is approximate** — Phase 4 CFG
  liveness sees AST-level locals; the actual ray stack carries a
  COMPILER-CHOSEN subset (some values are rematerialised after
  the call rather than spilled across it). We err on the side of
  flagging more than we should; the user can suppress per-call.
* **`buffer-load-width-vs-cache-line` requires per-wave reasoning**
  — at AST level we approximate "wave aggregation" as
  "lane-uniform offset + per-lane index in `[0, 4)` -> coalescable
  Load4". Heuristic; warn severity.

In all three cases the heuristic is a useful upper bound on
"places worth investigating manually." That's what Phase 7's
warn-severity is for. Real driver-accuracy claims are explicitly
out of scope for v0.7 (ADR 0016 decision driver
"Best-effort precision; rules ship at warn severity").

## Trade-offs we get by skipping the lowered-IR layer

* **Zero new external deps.** No DXC submodule, no spirv-tools,
  no LLVM, no per-platform prebuilt cache, no CI grep clause for
  DXC headers, no cross-platform LLVM build.
* **v0.7 ships in dev-days, not dev-weeks.** Sub-phase 7b.1 is
  ~150 LOC backward dataflow; 7b.2 is ~100 LOC heuristic; the
  four rule packs are conventional rule TUs.
* **All Phase 7 rules unblock simultaneously.** No "can't ship
  Pack DXR until DXC integrates" dependency chain.
* **The IR engine surface stays clean.** `IrInfo` is metadata-only
  but rules dispatch through `Stage::Ir` cleanly. A future v0.8+
  rule that genuinely needs lowered-DXIL access can land under a
  new ADR without breaking the public API; rules dispatched
  against today's metadata-only `IrInfo` continue to work.

## Implementation sub-phases (revised)

### Sub-phase 7b.1 — Liveness over Phase 4 CFG (sequential)

Single PR. Lands:

* `core/src/rules/util/liveness.{hpp,cpp}` — backward dataflow
  fixed-point iteration over the existing `ControlFlowInfo` storage.
  ~150 LOC. Operates on AST identifier strings (the CFG already
  exposes them via the uniformity oracle's variable tracking).
  Returns a `LivenessInfo` with per-CFG-node live-in / live-out
  sets keyed by AST byte-span.
* `tests/unit/test_liveness.cpp` — 4-6 cases covering: simple
  use-after-def, branch merge with phi-equivalent, loop carry,
  liveness across function calls (treat as full kill / full re-gen
  at the boundary).

Effort: ~1 dev day.

### Sub-phase 7b.2 — Register-pressure heuristic (sequential)

Single PR. Lands:

* `core/src/rules/util/register_pressure_ast.{hpp,cpp}` —
  `estimate_pressure(cfg, liveness, reflection)` returning a
  per-block estimate of live VGPRs. Bit-width comes from
  reflection types where available (cbuffer fields, parameters);
  defaults to 32 bits when the type can't be resolved.
* `tests/unit/test_register_pressure_ast.cpp` — 4 cases.

Effort: ~half a dev day.

### Sub-phase 7c — Four parallel rule packs (parallel)

After 7b.1 + 7b.2 land:

* `tests/fixtures/phase7/dxr.hlsl` and per-pack siblings receive
  the canonical HIT / SHOULD-NOT-HIT annotations.
* Each rule TU under `core/src/rules/` (15 new files).
* Each rule's doc page under `docs/rules/` (15 new files).
* Each rule's unit test under `tests/unit/` (15 new files).
* `core/src/rules/registry.cpp` gets 15 new factory entries.
* `core/CMakeLists.txt` and `tests/CMakeLists.txt` updated.

Effort: ~1 dev day per pack with parallel agents.

### v0.7.0 release

After all packs merge:

* Bump `core/src/version.cpp` -> "0.7.0".
* Bump `vscode-extension/package.json` -> "0.7.0".
* CHANGELOG entry summarising Phase 7 + the ADR-0016 -> ADR-0017
  pivot.
* Local clean build + ctest.
* Tag `v0.7.0`, push, GitHub Actions ships binaries + `.vsix`.

## Consequences

* Phase 7 ships v0.7.0 with 15 new rules at warn severity,
  bringing the total registered rule count to **169**.
* No new build-time dependency. Existing AST-only / reflection-only
  / CFG-only consumers see zero behaviour change.
* The `<shader_clippy/ir.hpp>` public header and `Stage::Ir` enum
  stay shipped (already in v0.6.8) but are documented as a
  metadata-only surface; the v0.7 rules that use `Stage::Ir` are
  ones that just need stage gating, not instruction walks.
* ADR 0016's "Open question" about per-format dispatch
  (DXIL vs SPIR-V) is moot for v0.7 — there's no format axis.
  If v0.8+ re-introduces a real IR layer it can re-open the
  question.

## Risks & mitigations

* **Risk: register-pressure heuristic is too noisy.** The
  source-level over-counting was discussed above. Mitigation
  is the same as ADR 0016: warn severity + tunable threshold
  + inline suppression. We will tune the default threshold
  in v0.7.x patch releases as user feedback comes in.
* **Risk: liveness on AST-level identifiers misses
  let-bindings / aliasing.** The Phase 4 CFG's variable
  tracking is the source of truth; this just consumes it.
  Same accuracy as the existing `groupshared-dead-store` /
  `cbuffer-divergent-index` rules that already use the CFG.
* **Risk: Phase 7 rules duplicate AST-pattern work that
  Phase 2 / 3 / 4 packs already do.** Mitigated by the
  per-rule audit above being explicit about which stage each
  rule sits at; the 15 rule TUs that land are net-new
  (the only overlap is `redundant-texture-sample` which
  borders on the existing `loop-invariant-sample` but is
  distinct: same-block CSE vs. loop-hoisted invariant).

## More Information

* Cross-references:
  - ADR 0007 §Phase 7 (the original 13 IR-level rules — re-routed
    by this ADR to AST / Reflection / ControlFlow stages).
  - ADR 0010 §"Risks" (`maybereorderthread-without-payload-shrink`
    re-routed to AST + ControlFlow, same liveness machinery as
    `live-state-across-traceray` exactly as ADR 0010 specified —
    just at the source level instead of post-codegen).
  - ADR 0011 §Phase 7 (the 2 LOCKED rules
    `groupshared-when-registers-suffice` and
    `buffer-load-width-vs-cache-line` re-routed to AST +
    Reflection + ControlFlow, same heuristic shape).
  - ADR 0013 (Phase 4 CFG infrastructure — this ADR's liveness
    extension lives next to the existing CFG storage).
  - ADR 0016 (Phase 7 IR-level analysis infrastructure — this
    ADR supersedes its sub-phase 7a.2-step2, 7b shared utilities
    over IR, and the entire DXC dependency chain. ADR 0016's
    `Stage::Ir`, public `IrInfo`, and 7a.1 / 7a.2-step1 stay
    in force unchanged).
