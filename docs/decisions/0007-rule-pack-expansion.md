---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [rules, roadmap]
---

# Rule-pack expansion (+41 rules)

## Context and Problem Statement

The Phase 0/1 ROADMAP shipped with ~50 rules across `math`, `bindings`, `texture`, `workgroup`, `interpolators`, `control-flow`, `numerical-safety`, plus a research-grade Phase 7 list. The original brainstorm hadn't covered modern shader-model territory: descriptor heap dynamic indexing (SM 6.6+), packed-math intrinsics + `dot4add` (SM 6.4+), variable rate shading, sampler feedback (SM 6.5+), mesh / amplification shaders (SM 6.5), DXR ray tracing, work graphs (SM 6.8), or several latent gaps in atomics, groupshared bank conflicts, helper-lane semantics, and FMA-fold opportunities.

A research pass produced 41 additional rule candidates. We need to:

1. Verify none duplicate existing ROADMAP entries.
2. Slot each into the right phase (AST-only, reflection-aware, control/data-flow, IR).
3. Group them so each batch is a thematic blog series — the project's reputation engine.
4. Document the brainstorm methodology so future expansions can be evaluated against the same bar.

## Decision Drivers

- Modern HLSL features (SM 6.4 → 6.8) are where the next several years of D3D12 + Vulkan game shipping happens; a linter that stops at SM 6.0 ages out fast.
- Rules with clear "wrong-pattern → right-pattern" structure produce blog posts; rules whose explanation is "it's complicated" don't pull weight.
- Each rule should slot into a phase whose tooling supports it: AST-only rules need only tree-sitter; binding / layout / stage-attribute rules need Slang reflection (Phase 3); divergence / live-range / atomic-pre-reduction rules need a CFG (Phase 4); ray-stack live-state needs IR (Phase 7).

## Considered Options

The decision space was: which rules to add. We considered the full 41 candidates from the brainstorm and accepted all 41, distributed across phases as below.

## Decision Outcome

All 41 rules added to the ROADMAP. Distribution by phase + category:

### Phase 2 — AST-only math (6 rules)

`manual-mad-decomposition`, `dot-on-axis-aligned-vector`, `length-then-divide`, `cross-with-up-vector`, `countbits-vs-manual-popcount`, `firstbit-vs-log2-trick`.

### Phase 3 — Reflection / type-aware (15 rules)

- **Bindings**: `descriptor-heap-no-non-uniform-marker`, `descriptor-heap-type-confusion`, `all-resources-bound-not-set`, `rov-without-earlydepthstencil`.
- **Texture / sampling**: `gather-cmp-vs-manual-pcf`, `texture-lod-bias-without-grad`.
- **Packed math / fp16**: `pack-clamp-on-prove-bounded`, `min16float-in-cbuffer-roundtrip`.
- **VRS / pixel shader**: `vrs-incompatible-output`, `sv-depth-vs-conservative-depth`.
- **Sampler feedback**: `feedback-write-wrong-stage`.
- **Mesh / amplification**: `mesh-numthreads-over-128`, `mesh-output-decl-exceeds-256`, `as-payload-over-16k`.
- **DXR**: `missing-ray-flag-cull-non-opaque`.
- **Work graphs**: `nodeid-implicit-mismatch`.

### Phase 4 — Control flow + light data flow (19 rules)

- **Control-flow / divergence**: `sample-in-loop-implicit-grad`, `early-z-disabled-by-conditional-discard`, `wave-intrinsic-helper-lane-hazard`, `wave-active-all-equal-precheck`, `cbuffer-divergent-index`.
- **Atomics / groupshared**: `interlocked-bin-without-wave-prereduce`, `interlocked-float-bit-cast-trick`, `groupshared-stride-32-bank-conflict`, `groupshared-write-then-no-barrier-read`.
- **Packed math**: `pack-then-unpack-roundtrip`, `dot4add-opportunity`.
- **Mesh / amplification**: `setmeshoutputcounts-in-divergent-cf`.
- **DXR**: `tracerray-conditional`, `anyhit-heavy-work`, `inline-rayquery-when-pipeline-better` / `pipeline-when-inline-better`.
- **Sampler feedback**: `feedback-every-sample`.
- **Work graphs**: `outputcomplete-missing`, `quad-or-derivative-in-thread-launch-node`.

### Phase 7 — Stretch / IR (1 rule)

- **DXR**: `live-state-across-traceray` (live-range across `TraceRay` → ray-stack spill).

### Rule-pack catalog (Phase 6 launch)

The togglable categories grow from `math, bindings, texture, workgroup, control-flow` to `math, bindings, texture, workgroup, control-flow, vrs, sampler-feedback, mesh, dxr, work-graphs`.

### Brainstorm methodology

The 41 candidates were sourced by walking the following surfaces and asking "what portable anti-pattern is observable from AST + reflection + a CFG, has a clear right answer, and has a one-paragraph GPU explanation":

- HLSL Shader Model 6.4 → 6.8 spec deltas (packed-math, sampler feedback, mesh shaders, work graphs).
- The DXR spec (any-hit work boundaries, ray-flag culling, ray-payload sizing, ray-stack live state).
- IHV optimization guides (NVIDIA scalarization with `WaveActiveAllEqual`, AMD groupshared bank-conflict patterns, Intel uniform-CBV serialization).
- D3D12 PSO validation rules that produce hard creation failures (mesh `[numthreads]` cap, AS payload cap, `OutputComplete()` pairing).
- Hardware microarchitecture documentation for FMA folding, scalar-vs-vector loads, LDS bank structure.

Each candidate was triaged on (a) is the wrong pattern observable in the IR / AST + reflection we have, (b) is the right pattern unambiguous, (c) is the GPU reason a reader can summarize in a paragraph. Rules that fail any of the three are deferred or dropped.

This ADR is the canonical reference for the rules ROADMAP.md now lists. The full per-rule notes (one-line "what it detects, why it costs, what to write instead") are preserved verbatim in `_research/hlsl-rules-brainstorm.md`.

### Consequences

Good:

- The ROADMAP now covers the modern shader-model surface that AAA / engine teams are actually shipping in 2026.
- Each phase batch maps to a thematic blog series — Phase 4 alone gives us atomics, packed-math, ray tracing, and work-graphs posts.
- The DXR / mesh / work-graphs categories give the project a forward-looking story that distinguishes it from `dxc` warnings.

Bad:

- Phase 4 grows from 13 rules to 32 rules. The control-flow + light-data-flow infrastructure must absorb more variety than the original phase scope assumed.
- Several rules need shader-stage discrimination from Slang reflection (PS-only, mesh-shader-only, work-graph-only) — the rule registry needs a stage-tag mechanism (already proposed in ADR 0003 / architecture review §3 `Rule::stage()`).
- Brainstorm was AI-generated and the original output mislabelled the count ("30" header, 41 actual rules). Each rule still needs its own validation pass (does Slang reflection actually expose the data we need? Does a fixture exist?) before it lands in the implementation queue.

### Confirmation

- ROADMAP.md Phases 2 / 3 / 4 / 7 now contain all 41 rules, grouped by category.
- ROADMAP.md Phase 6 rule-pack catalog enumerates the new togglable categories.
- For each rule, before it ships:
  - A `tests/fixtures/phaseN/<category>.hlsl` entry with a `// HIT(rule-name)` marker (per the existing `tests/fixtures/README.md` convention).
  - A blog-post draft under `docs/rules/<rule-name>.md` explaining the GPU reason it matters.
- Future expansions add an addendum ADR (this one is not edited).

## Pros and Cons of the Options

### Adopt all 41

- Good: maximum surface coverage; modern shader-model story is honest.
- Bad: more rules to schedule + write blog posts for. Acceptable — each rule is independent and the schedule is per-phase, not per-rule.

### Adopt a subset

- Good: smaller surface to validate.
- Bad: arbitrary; each rule is independent and meets the brainstorm bar (observable, unambiguous, one-paragraph GPU reason). No principled subset exists.

### Defer all 41 to "post-1.0"

- Good: keep the v0.5 launch surface tight.
- Bad: launches `hlsl-clippy` as an SM 6.0-era tool; fails the "modern shader-model coverage" reputation goal.

## Links

- Verbatim research: `_research/hlsl-rules-brainstorm.md` (preserved as-is, including the mislabelled count).
- Related: ADR 0001 (Slang reflection availability for Phase 3 rules), ADR 0003 (`Rule::stage()` mechanism), ROADMAP.md "Phases 2-7".
