---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [rules, sm-6.9, sm-6.8, sm-6.7, ser, work-graphs, cooperative-vectors]
---

# SM 6.9 rule expansion (+36 rules)

## Context and Problem Statement

ADR 0007 mined the SM 6.4 → 6.8 surface (descriptor heaps, packed math, sampler feedback, mesh shaders, basic work graphs, DXR 1.1) and produced 41 lint candidates. That pass landed before **DXR 1.2 + SM 6.9** shipped retail in Agility SDK 1.619 (February 2026), and it under-mined the SM 6.7 helper-lane / quad surface. SM 6.9 is the largest single shader-model delta since SM 6.5: Shader Execution Reordering (SER) with `dx::HitObject` and `dx::MaybeReorderThread`, Cooperative Vectors (matrix-vector multiply on tensor / WMMA cores), Long Vectors (`vector<T, N>` for `5 ≤ N ≤ 1024`), Opacity Micromaps in DXR 1.2, and Mesh Nodes in Work Graphs (preview). Each surface has a tightly specified set of validation rules and performance footguns that are observable at the AST + Slang-reflection layer shader-clippy already plans to operate on.

A second research pass (`_research/sm69-rule-expansion.md`) produced 36 candidate rules across SER, Cooperative Vectors, Long Vectors, OMM, the SM 6.7 wave/quad surface, SM 6.8 attributes, mesh nodes, and SM 6.9 numerical-special intrinsics. Every candidate was cross-checked against the current ROADMAP.md and ADR 0007 — none duplicate existing rules. The decision now is whether to adopt the pack, and if so, how to slot the rules across phases without distending Phase 4's already-large scope.

## Decision Drivers

- **Modern shader-model coverage.** AAA and engine teams shipping in 2026 are targeting SM 6.9 as soon as Agility SDK 1.619 propagates. A linter that stops at SM 6.8 ages out exactly as the SER / cooperative-vector / OMM / long-vector wave breaks. The reputation play for this project is being early on SM 6.9 perf rules — the same play that ADR 0007 made for SM 6.6 → 6.8.
- **Slang reflection coverage.** Slang already exposes `HitObject` (with NV-only methods clearly fenced off), the cooperative-vector intrinsics via the `linalg.h` namespace, the SM 6.9 long-vector typing rules, and the OMM ray-flag / template-parameter surface. Phase 3's reflection-aware machinery can land most of the SER / OMM / cooperative-vector rules without new infrastructure. ADR 0001 (Slang as the compiler) is paying off.
- **Per-phase load shaping.** ADR 0007 grew Phase 4 from 13 → 32 rules. Adding 10 more Phase 4 rules from SM 6.9 takes it to 42 — viable, but only because each new rule shares the SER / wave-helper-lane / Cooperative-Vector uniformity-analysis machinery. The ADR has to argue that this isn't infinite scope creep.
- **Spec-status discipline.** Mesh nodes in work graphs are *preview*, not retail; clustered geometry / CBLAS is *Accepted but SM 6.10*; LSS is *vendor-only*. The ADR has to mark each candidate's status so we don't ship lints against APIs that change underneath us.
- **Blog series throughput.** Phase 3 alone yields four thematic posts (SER, Cooperative Vectors, Long Vectors, OMM). Phase 4 yields the SER coherence-hint / `[reordercoherent]` post and the SM 6.7 helper-lane attribute post. Each rule that lands carries one post; the schedule is the same per-phase one ADR 0007 set up.

## Considered Options

1. **Implement all 36 candidates, distributed by phase.** Maximum coverage. Phase 3 absorbs the bulk of the SER / OMM / Cooperative-Vector / Long-Vector validation; Phase 4 picks up the SER coherence + uniformity rules; Phase 7 picks up the live-state rule. ADR 0007's house style.
2. **Implement only the SER pack first, defer the rest.** Tightest reputation-bet — SER is the highest-leverage SM 6.9 feature for AAA path tracers and the rule pack with the most documented footguns. Defers Cooperative Vectors, Long Vectors, OMM, the SM 6.7 quad/wave surface.
3. **Defer all 36 to post-1.0.** Keep v0.5 launch surface tight; pick up SM 6.9 once retail adoption has settled.
4. **Split by phase (chosen).** Adopt all 36, but slot them according to the data-flow each rule needs. Phase 3 takes the type / reflection / template-arg rules. Phase 4 takes the uniformity / CFG / barrier rules. Phase 7 takes the live-range rule. Mesh-node rules ship behind an `experimental.work-graph-mesh-nodes` config flag until the preview API locks.

## Decision Outcome

**Option 4** — split by phase, with explicit spec-status gates on the preview / vendor-only territory.

The candidates and their phase placements (full notes in `_research/sm69-rule-expansion.md`):

### Phase 2 — AST-only (2 rules)

- `long-vector-non-elementwise-intrinsic` (#20). Pure AST — intrinsic name + vector-type literal. Spec: long vectors only on elementwise intrinsics; `cross` / `length` / `normalize` / `transpose` / `mul-with-matrix` are explicitly out.
- `wavesize-range-disordered` (#29). Constant-folded attribute argument check.

### Phase 3 — Reflection / type-aware (23 rules)

**SER (4):**
- `hitobject-stored-in-memory` (#1) — UB.
- `maybereorderthread-outside-raygen` (#2) — UB.
- `hitobject-construct-outside-allowed-stages` (#3) — UB.

**Cooperative Vectors (5):**
- `coopvec-non-optimal-matrix-layout` (#12) — perf.
- `coopvec-fp8-with-non-optimal-layout` (#13) — UB.
- `coopvec-stride-mismatch` (#14) — UB.
- `coopvec-base-offset-misaligned` (#15) — UB.
- `coopvec-transpose-without-feature-check` (#17) — runtime failure.

**Long Vectors (3):**
- `long-vector-in-cbuffer-or-signature` (#18) — compile error / quick-fix-able.
- `long-vector-typed-buffer-load` (#19) — UB / compile error.
- `long-vector-bytebuf-load-misaligned` (#21) — perf / UB on some implementations.

**Opacity Micromaps (3):**
- `omm-rayquery-force-2state-without-allow-flag` (#22) — UB.
- `omm-allocaterayquery2-non-const-flags` (#23) — compile error.
- `omm-traceray-force-omm-2state-without-pipeline-flag` (#24) — UB.

**SM 6.7 wave / quad (2):**
- `waveops-include-helper-lanes-on-non-pixel` (#25) — promote DXC warning to hard rule.
- `quadany-quadall-non-quad-stage` (#27) — UB.

**SM 6.8 attributes (2):**
- `wavesize-fixed-on-sm68-target` (#30) — suggestion.
- `startvertexlocation-not-vs-input` (#31) — UB.

**Mesh nodes (3, gated `experimental.work-graph-mesh-nodes`):**
- `mesh-node-not-leaf` (#32) — UB.
- `mesh-node-missing-output-topology` (#33) — compile error.
- `mesh-node-uses-vertex-shader-pipeline` (#34) — compile error.

**SM 6.9 numerical specials (1):**
- `isspecialfloat-implicit-fp16-promotion` (#35) — perf / suggestion.
- `isnormal-pre-sm69` (#36) — compile error.

### Phase 4 — Control flow + light data flow (10 rules)

**SER (5):**
- `hitobject-passed-to-non-inlined-fn` (#4) — needs inter-procedural reasoning. UB.
- `coherence-hint-redundant-bits` (#6) — bit-range analysis.
- `coherence-hint-encodes-shader-type` (#7) — taint-style analysis from `IsHit()` / `GetShaderTableIndex()`.
- `reordercoherent-uav-missing-barrier` (#8) — CFG that crosses reorder points.
- `hitobject-invoke-after-recursion-cap` (#9) — call-graph + recursion accounting.
- `fromrayquery-invoke-without-shader-table` (#10) — definite-assignment-style on `SetShaderTableIndex`.
- `ser-trace-then-invoke-without-reorder` (#11) — perf / missed-opportunity.

**Cooperative Vectors (1):**
- `coopvec-non-uniform-matrix-handle` (#16) — uniformity analysis on the matrix-handle / offset / stride / interpretation arguments.

**SM 6.7 wave / quad (2):**
- `wave-reduction-pixel-without-helper-attribute` (#26) — data-flow from reduction result to derivative-bearing op.
- `quadany-replaceable-with-derivative-uniform-branch` (#28) — branch-shape detection + sample / derivative inside.

### Phase 7 — Stretch / IR (1 rule)

- `maybereorderthread-without-payload-shrink` (#5) — IR-level live-range analysis at the `MaybeReorderThread` call site. Same machinery the existing Phase 7 `live-state-across-traceray` rule needs; ship together.

## Confirmation

| # | id | phase | why-now / defer |
|--|--|--|--|
| 1 | `hitobject-stored-in-memory` | 3 | SM 6.9 retail; UB; observable from Slang reflection. |
| 2 | `maybereorderthread-outside-raygen` | 3 | Hard rule; entry-stage from reflection. |
| 3 | `hitobject-construct-outside-allowed-stages` | 3 | Hard rule; entry-stage from reflection. |
| 4 | `hitobject-passed-to-non-inlined-fn` | 4 | Inter-procedural; needs Phase 4 call-graph. |
| 5 | `maybereorderthread-without-payload-shrink` | 7 | Live-state estimate; share with `live-state-across-traceray`. |
| 6 | `coherence-hint-redundant-bits` | 4 | Bit-range narrowing; share with `pack-clamp-on-prove-bounded` machinery. |
| 7 | `coherence-hint-encodes-shader-type` | 4 | Taint analysis; ship after Phase 4 data-flow lands. |
| 8 | `reordercoherent-uav-missing-barrier` | 4 | CFG-crossing-reorder-point; share with barrier-in-divergent-CF infra. |
| 9 | `hitobject-invoke-after-recursion-cap` | 4 | Call-graph + recursion budget. |
| 10 | `fromrayquery-invoke-without-shader-table` | 4 | Definite-assignment data-flow. |
| 11 | `ser-trace-then-invoke-without-reorder` | 4 | Reachability between two intrinsic calls. |
| 12 | `coopvec-non-optimal-matrix-layout` | 3 | Layout enum visible from reflection. |
| 13 | `coopvec-fp8-with-non-optimal-layout` | 3 | Hard validation; reflection-trivial. |
| 14 | `coopvec-stride-mismatch` | 3 | Constant-fold stride. |
| 15 | `coopvec-base-offset-misaligned` | 3 | Constant-fold offset; alignment annotation from reflection. |
| 16 | `coopvec-non-uniform-matrix-handle` | 4 | Wave-uniformity analysis; share with `cbuffer-divergent-index`. |
| 17 | `coopvec-transpose-without-feature-check` | 3 | Runtime-failure prevention; AST + reflection. |
| 18 | `long-vector-in-cbuffer-or-signature` | 3 | Reflection sees the boundary. |
| 19 | `long-vector-typed-buffer-load` | 3 | Resource-type from reflection. |
| 20 | `long-vector-non-elementwise-intrinsic` | 2 | Pure AST. Trivial. |
| 21 | `long-vector-bytebuf-load-misaligned` | 3 | Constant-offset alignment check. |
| 22 | `omm-rayquery-force-2state-without-allow-flag` | 3 | Template-arg parsing. |
| 23 | `omm-allocaterayquery2-non-const-flags` | 3 | Constant-fold; defer to DXC for some, but a structured diagnostic is friendlier. |
| 24 | `omm-traceray-force-omm-2state-without-pipeline-flag` | 3 | Project-level — pipeline subobject from Slang reflection. |
| 25 | `waveops-include-helper-lanes-on-non-pixel` | 3 | DXC warns; promote to hard rule. |
| 26 | `wave-reduction-pixel-without-helper-attribute` | 4 | Data-flow from wave reduction → derivative. |
| 27 | `quadany-quadall-non-quad-stage` | 3 | Stage from reflection. |
| 28 | `quadany-replaceable-with-derivative-uniform-branch` | 4 | Suggestion-grade; needs branch-shape pattern. |
| 29 | `wavesize-range-disordered` | 2 | AST + constant-fold. |
| 30 | `wavesize-fixed-on-sm68-target` | 3 | Target SM from compile options. |
| 31 | `startvertexlocation-not-vs-input` | 3 | Reflection sees stage + semantic. |
| 32 | `mesh-node-not-leaf` | 3 (gated) | Preview API; gate behind config until lock. |
| 33 | `mesh-node-missing-output-topology` | 3 (gated) | Preview API; gate. |
| 34 | `mesh-node-uses-vertex-shader-pipeline` | 3 (gated) | Preview API; gate. |
| 35 | `isspecialfloat-implicit-fp16-promotion` | 3 | Target SM + arg type. |
| 36 | `isnormal-pre-sm69` | 3 | Target SM + intrinsic name. |

For each rule, before it ships:

- A `tests/fixtures/phaseN/<category>.hlsl` fixture with a `// HIT(rule-name)` marker (per the fixtures convention used in ADR 0009).
- A blog-post draft under `docs/rules/<rule-name>.md` explaining the GPU reason it matters. SER, Cooperative Vector, Long Vector, and OMM each get a category-overview post in addition to the per-rule pages.
- Spec-status front-matter in the rule's blog post: which DXC / Slang version implements it, which Agility SDK exposes it, and whether it's preview / retail / vendor-only.
- Mesh-node rules (#32–#34) hidden behind `[experimental] work-graph-mesh-nodes = true` in `.shader-clippy.toml` until the preview API in Agility SDK is finalized.

The Phase 6 rule-pack catalog grows from ADR 0007's `math, bindings, texture, workgroup, control-flow, vrs, sampler-feedback, mesh, dxr, work-graphs` to also include `ser`, `cooperative-vector`, `long-vectors`, `opacity-micromaps`, and `wave-helper-lane`.

## Pros and Cons of the Options

### Option 1 — implement all 36, distributed by phase

- Good: maximum coverage; the linter is honest about SM 6.9.
- Bad: distributed-by-phase implicit in the option name without spec-status gating means we'd ship lints against the preview mesh-node API, which can change.

### Option 2 — SER-pack-first

- Good: tightest reputation bet; SER is the highest-impact SM 6.9 feature.
- Bad: leaves Cooperative Vectors / Long Vectors / OMM / SM 6.7 surface uncovered. The Cooperative Vector and Long Vector rules are mostly Phase 3 reflection rules — same effort tier as ADR 0007's Phase 3 rules. Splitting them out adds no real risk reduction.

### Option 3 — defer to post-1.0

- Good: smaller v0.5 launch surface.
- Bad: launches as an SM 6.8-era tool exactly when SM 6.9 retail is live. Repeats the failure mode ADR 0007 was written to avoid.

### Option 4 — split by phase, with experimental gating (chosen)

- Good: phase placement matches infrastructure availability — every rule can be implemented when its phase's machinery lands. Mesh-node rules behind a config flag means we ship them when ready and don't have to revert if the preview API moves. Cooperative-Vector and SER reach Phase 3 alongside the Phase-3 reflection-rule pack, so they get implementation attention naturally.
- Good: blog-post throughput is preserved — Phase 3 is now four named series posts (SER, Cooperative Vector, Long Vector, OMM) on top of the ADR 0007 Phase 3 set; Phase 4 picks up coherence + helper-lane.
- Bad: Phase 4 grows from ADR 0007's 32 rules to 42 rules. Acceptable because the new rules share machinery (SER coherence-hint analysis ≈ taint analysis we'd write for `cbuffer-divergent-index`; uniformity for cooperative-vector is the same machinery as `wave-active-all-equal-precheck`).
- Bad: spec-status discipline adds a config gate (one new key) and per-rule version metadata (already needed for ADR 0007 rules).

## Links

- Verbatim research: `_research/sm69-rule-expansion.md`.
- Source proposals: [SM 6.9 spec](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_9.html), [proposal 0027 SER](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md), [proposal 0029 cooperative-vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md), [proposal 0030 dxil-vectors](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0030-dxil-vectors.md), [proposal 0024 opacity-micromaps](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0024-opacity-micromaps.md), [SM 6.7 spec](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_7.html), [SM 6.8 spec](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_8.html), [Mesh nodes in work graphs preview](https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/).
- Best-practices sources: [NVIDIA SER perf blog](https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/), [Indiana Jones SER live-state case study](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/), [Khronos VK_EXT_ray_tracing_invocation_reorder blog](https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder).
- Related: ADR 0001 (Slang reflection), ADR 0003 (`Rule::stage()` mechanism), ADR 0007 (the SM 6.4 → 6.8 rule pack this ADR extends), ROADMAP.md "Phases 2-7".
