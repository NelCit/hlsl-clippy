---
status: Accepted
date: 2026-05-01
deciders: NelCit
tags: [rules, candidates, per-phase-plan, groupshared, buffers, samplers, root-signature, mesh, compute, numerical, wave-quad-extras, texture-format, divergence-hints]
---

# Candidate rule adoption — underexplored portable surfaces (per-phase plan)

## Context and Problem Statement

A 2026-05-01 brainstorm pass surfaced 63 additional candidate lint rules
covering surfaces that ADR 0007 (SM 6.4 -> 6.8 pack, +41 rules) and ADR
0010 (SM 6.7/6.8/6.9 pack, +36 rules) under-covered: groupshared / LDS
micro-architecture, ByteAddressBuffer alignment, root-signature
ergonomics, mesh / amplification edge cases, helper-lane / quad
subtleties, texture-format swizzle traps, and divergence-hint mistakes.
The candidates are listed in `ROADMAP.md` lines ~299-398 under
"Candidate rule expansion (research, not yet locked)" with an explicit
disclaimer that locking any of them needs an ADR or addendum.

This ADR is the lock. It curates the 63 down to a shipping subset, slots
each LOCKED rule into the phase whose machinery supports it, and lays
out per-phase implementation plans in the same shape as ADR 0008 (Phase
1) and ADR 0009 (Phase 2 — shared-utilities PR + parallel category
packs). Rules whose detection requires research that has not landed are
DEFERRED with a one-line reason; rules that duplicate or are subsumed by
already-locked rules are DROPPED.

Each LOCKED rule gets a doc page under `docs/rules/<id>.md` per
`_template.md`. Per the CLAUDE.md "rule + blog post pair" convention,
each LOCKED rule also seeds a blog post under `docs/blog/`. Doc pages
can be authored in parallel because every LOCKED rule carries the
standard `Pre-v0 status` notice until its implementation lands.

## Decision Drivers

- **Portability across IHVs.** Rules must observably bite the same way
  on RDNA 2/3, NVIDIA Turing/Ada, and Intel Xe-HPG. Vendor-only
  micro-optimisations are deferred or dropped — vendor analyzers (RGA,
  Nsight, GPA) own that ground per the ROADMAP "Non-goals" section.
- **AST + reflection feasibility, by phase.** A rule lands in Phase N
  when Phase N's infrastructure can implement it. AST-only rules in
  Phase 2; reflection / type / stage-aware rules in Phase 3;
  uniformity / CFG / def-use rules in Phase 4; live-range / register
  estimation rules in Phase 7. Same discipline as ADR 0007 and ADR 0010.
- **No near-duplication of locked rules.** Three brainstorm callouts
  flagged near-duplicates with locked rules; this ADR adjudicates each
  explicitly. One additional near-duplicate not flagged by the
  brainstorm pass is also adjudicated below.
- **Blog-post seed potential.** Per CLAUDE.md, every shipped rule
  carries a companion blog post explaining the GPU mechanism. Rules
  whose explanation is "it's complicated and vendor-specific" don't
  meet the bar and are deferred or dropped.
- **Phase 4 load shaping.** ADR 0007 grew Phase 4 to 32 rules; ADR 0010
  added 10 more (total 42). This ADR adds 16 more (total 58). Each new
  Phase 4 rule must share infrastructure with rules ADR 0007 or 0010
  already justified — the uniformity / CFG / def-use machinery is
  amortised, not duplicated.

## Considered Options

### Option A — lock all 63

- Good: maximum coverage; no follow-up triage needed.
- Bad: ships rules whose detection is heuristic-only (vendor-specific
  occupancy modelling, render-graph aliasing) at the same status tier
  as well-grounded rules; produces blog posts that read as
  "it's complicated", which damages the project's reputation goal.

### Option B — lock none, leave the candidates section as-is

- Good: keeps Phases 2-7 from growing further.
- Bad: leaves real portable footguns (groupshared bank conflicts beyond
  stride-32, ByteAddressBuffer misaligned widened loads,
  comparison-sampler / static-sampler misuse, `[flatten]` on a uniform
  branch) unowned. ROADMAP becomes a graveyard for "we thought about
  it once".

### Option C — curated subset (~40 rules) split across Phases 2-5 + Phase 7 (chosen)

- Good: every LOCKED rule meets the same bar as ADR 0007 / ADR 0010
  rules (observable, unambiguous, one-paragraph GPU reason). Phase
  placement matches infrastructure availability. The DEFERRED bucket
  documents what needs more research without committing to it. The
  DROPPED bucket records the duplicate adjudication so the same rule
  doesn't resurface in the next brainstorm.
- Good: the per-phase implementation plans mirror ADR 0009's
  parallel-pack pattern — shared utilities first (where applicable),
  then per-category packs that 2-3 implementers can run concurrently.
- Bad: Phase 3 grows by 17 rules and Phase 4 grows by 16. Reviewer
  load increases; mitigated by the parallel-pack split and by the
  shared infrastructure already justified in ADR 0007 / ADR 0010.

## Decision Outcome

**Option C — curated subset, per-phase plan.**

### Verdict tally

| Verdict | Count |
|---|---|
| LOCKED | 41 |
| DEFERRED | 20 |
| DROPPED | 2 |
| **Total** | **63** |

LOCKED breakdown by phase:

| Phase | Rules |
|---|---|
| Phase 2 | 6 |
| Phase 3 | 17 |
| Phase 4 | 16 |
| Phase 5 | 0 |
| Phase 7 | 2 |

The per-phase verdicts and implementation plans follow.

---

## Phase 2 (AST-only) implementation plan

**Scope.** Six rules expressible as pure tree-sitter query patterns or
constant-fold checks on AST literals. No reflection, no CFG, no
uniformity analysis. Each rule produces a one-paragraph GPU
explanation; each is a doc-page + blog-post pair.

**LOCKED rules:**

- `groupshared-volatile`: `volatile` qualifier on a `groupshared`
  declaration. *GPU reason:* `volatile` on groupshared is meaningless
  under the HLSL memory model (use `GroupMemoryBarrier*` or
  `globallycoherent` on UAVs); it confuses the optimiser into
  pessimising LDS scheduling on every IHV.
- `lerp-on-bool-cond`: `lerp(a, b, (float)cond)` where `cond` is a
  bool. *GPU reason:* produces `select` codegen on most IHVs but a true
  `lerp` (mul + mad) on others; portable form is `cond ? b : a` or
  explicit `select`.
- `select-vs-lerp-of-constant`: `lerp(K1, K2, t)` with K1/K2 both
  constants. *GPU reason:* the compiler may not fold to
  `K1 + (K2-K1)*t` portably; explicit `mad(t, K2-K1, K1)` makes the
  intent compile-portable.
- `redundant-unorm-snorm-conversion`: explicit `* (1.0/255.0)` after
  sampling a UNORM texture. *GPU reason:* UNORM sampling already
  returns `[0,1]`; the divide is dead arithmetic on every IHV. (AST-
  only because the heuristic fires on the literal `1.0/255.0`; the
  reflection-aware tightening — confirm the source is UNORM — is
  filed as a Phase 3 follow-up.)
- `wavereadlaneat-constant-zero-to-readfirst`: `WaveReadLaneAt(x, 0)`.
  *GPU reason:* `WaveReadLaneFirst(x)` is the idiomatic spelling and
  lets the compiler skip the lane-index broadcast on RDNA / Ada.
- `loop-attribute-conflict`: both `[unroll]` and `[loop]` on the same
  loop, or `[unroll(N)]` with N > configurable threshold (default 32).
  *GPU reason:* compiler silently picks one; the conflict is almost
  always a refactor leftover.

**Pack split.** Six rules is below the threshold where per-category
parallelisation pays. Mirroring ADR 0009's smaller `misc-pack` shape:
ship as one PR, **`feat(rules): phase-2-candidates-pack`**. No new
shared utilities — the existing `core/src/rules/util/` (call_match,
literal_match, pure_expr, fix_builder) covers every rule. The
`groupshared-volatile` rule is a one-line declaration-modifier check
that touches no shared helpers.

**Estimated effort.** 1-2 dev days for the rule code + tests + golden
snapshots; doc-page authoring runs in parallel and is the schedule-
critical path (3-4 days for six pages with the GPU-reason paragraph).

Doc pages for these rules SHOULD be authored in parallel via the
standard `_template.md`; each carries the `Pre-v0 status` notice until
implementation lands.

---

## Phase 3 (reflection-aware) implementation plan

**Scope.** Seventeen rules that need Slang reflection — resource type,
sampler descriptor state, root-signature shape, entry-stage tag, target
shader model, `[numthreads]` / `[WaveSize]` attribute values, or
texture format. Phase 3 infrastructure (per ADR 0007 §Phase 3 + ADR
0010 §Phase 3) supplies all of the above.

**LOCKED rules:**

*Buffer access:*

- `byteaddressbuffer-load-misaligned`: `Load2`/`Load3`/`Load4` on
  `ByteAddressBuffer` at constant offset failing the natural-alignment
  check (8/12/16). *GPU reason:* under-aligned widened loads either
  fault or split into single-DWORD reads on RDNA / Turing / Ada /
  Xe-HPG.
- `byteaddressbuffer-narrow-when-typed-fits`: `ByteAddressBuffer.Load4`
  of a POD that exactly matches a `Buffer<float4>` /
  `StructuredBuffer<T>` view. *GPU reason:* typed views go through the
  texture cache on most IHVs; ByteAddressBuffer goes through the K$
  on RDNA and L1 on Turing/Ada — one path is usually wrong for the
  access pattern. (Subsumes the dropped
  `byteaddressbuffer-when-typed-load-suffices` candidate.)
- `structured-buffer-stride-not-cache-aligned`: stride a multiple of 4
  but not of 16 / 32 / 64 (configurable cache-line target).
  *GPU reason:* every stride straddles two cache lines on RDNA /
  Turing / Ada; distinct from the locked `structured-buffer-stride-
  mismatch` which targets HLSL packing rules.

*Groupshared (reflection-tagged):*

- `groupshared-union-aliased`: groupshared declaration of two distinct
  typed views over the same offset (manual `asuint` round-trips or a
  struct hack). *GPU reason:* the optimiser cannot reason about LDS
  aliasing and falls back to round-tripping every access through
  memory.
- `groupshared-16bit-unpacked`: `groupshared min16float` /
  `groupshared uint16_t` arrays where every access widens to 32 bits
  before use. *GPU reason:* RDNA 2/3 packs 16-bit LDS lanes 2-per-bank
  only when consumed via packed-math intrinsics; widening at the load
  site collapses the saving.

*Sampler / static sampler:*

- `static-sampler-when-dynamic-used`: a sampler whose state never
  varies across draws. *GPU reason:* static samplers cost no
  descriptor slot on D3D12 and are pre-resident on every IHV.
- `mip-clamp-zero-on-mipped-texture`: `MaxLOD = 0` (or `MinMipLevel = 0`
  clamp) on a sampler bound to a fully-mipped texture. *GPU reason:*
  silently disables all mip filtering, costs bandwidth (always reads
  mip 0) and aliases on terrain / streaming surfaces.
- `comparison-sampler-without-comparison-op`: `SamplerComparisonState`
  declared but only `Sample`/`SampleLevel` (non-`Cmp` variants)
  called. *GPU reason:* wastes a sampler descriptor slot and trains
  readers to expect PCF where there is none.
- `anisotropy-without-anisotropic-filter`: `MaxAnisotropy > 1` on a
  sampler whose `Filter` doesn't request anisotropic filtering.
  *GPU reason:* silently ignored on every IHV; surfaces author intent
  regression.
- `sampler-feedback-without-streaming-flag`:
  `WriteSamplerFeedback*` used without a corresponding tiled-resource
  binding visible in reflection. *GPU reason:* sampler feedback that
  doesn't feed a streaming system is dead bandwidth on IHVs that
  materialise the feedback texture.

*Root signature / cbuffer ergonomics:*

- `cbuffer-large-fits-rootcbv-not-table`: cbuffer ≤ 64 KB referenced
  once per dispatch where a root CBV would dodge the descriptor-table
  indirection. *GPU reason:* root CBVs save a descriptor heap
  dereference on every IHV. Companion to the locked
  `cbuffer-fits-rootconstants`.

*Compute-pipeline shape:*

- `compute-dispatch-grid-shape-vs-quad`: `[numthreads(N,1,1)]` chosen
  for a kernel that reads `ddx`/`ddy` (compute-quad derivatives).
  *GPU reason:* SM 6.6 compute-quad derivatives expect a 2x2 quad in
  the X/Y plane; 1D dispatch produces nonsense derivatives.
- `wavesize-attribute-missing`: kernel uses wave intrinsics in a way
  whose result depends on wave size and lacks `[WaveSize(N)]` /
  `[WaveSize(min, max)]`. *GPU reason:* without the attribute, RDNA
  1/2/3 may run wave32 or wave64, Turing/Ada always wave32, Xe-HPG
  wave8/16/32; results that index by lane count silently change.

*Wave / lane portability:*

- `wavereadlaneat-constant-non-zero-portability`: `WaveReadLaneAt(x, K)`
  with constant K when wave size is not pinned via `[WaveSize]`.
  *GPU reason:* K may be out of range on wave32 vs wave64; surfaces a
  portability bug between RDNA wave64 and Ada wave32.

*Texture format:*

- `bgra-rgba-swizzle-mismatch`: shader reads `.rgba` from a
  `Texture2D<float4>` whose binding maps a `DXGI_FORMAT_B8G8R8A8_UNORM`
  resource without a corresponding `.bgra` swizzle. *GPU reason:*
  silently inverts red and blue channels; real bug for IMGUI / UI
  pipelines that mix swap-chain BGRA with R8G8B8A8 SRGB sampling.
- `manual-srgb-conversion`: hand-rolled gamma 2.2 / sRGB transfer
  where the resource format already carries the sRGB conversion.
  *GPU reason:* double-applies the curve; common when migrating from
  `R8G8B8A8_UNORM` to `R8G8B8A8_UNORM_SRGB`.

*Resource state (documentation-grade):*

- `uav-srv-implicit-transition-assumed`: shader writes UAV `U` then
  reads SRV `S` where reflection notes `U` and `S` alias.
  *GPU reason:* D3D12 requires an explicit barrier; surfacing the
  alias from reflection lets the developer audit the application-side
  barrier. Suggestion-grade, no fix.

**Pack split.** Mirroring ADR 0009's three-pack pattern, the 17
Phase 3 rules split across three thematic packs that 3 implementers
can run concurrently after one shared-utilities PR lands:

- **Shared-utilities PR (lands first).** `core/src/rules/util/`
  additions for reflection-aware rules: `reflect_resource.hpp`
  (resource-type query helper over Slang reflection),
  `reflect_sampler.hpp` (sampler-descriptor field accessors),
  `reflect_stage.hpp` (entry-stage / target-SM accessors). ~150 LOC
  + unit tests.
- **PR A — buffers + groupshared-typed-pack** (5 rules):
  `byteaddressbuffer-load-misaligned`,
  `byteaddressbuffer-narrow-when-typed-fits`,
  `structured-buffer-stride-not-cache-aligned`,
  `groupshared-union-aliased`, `groupshared-16bit-unpacked`.
- **PR B — samplers + texture-format-pack** (7 rules):
  `static-sampler-when-dynamic-used`,
  `mip-clamp-zero-on-mipped-texture`,
  `comparison-sampler-without-comparison-op`,
  `anisotropy-without-anisotropic-filter`,
  `sampler-feedback-without-streaming-flag`,
  `bgra-rgba-swizzle-mismatch`, `manual-srgb-conversion`.
- **PR C — root-sig + compute + wave + state-pack** (5 rules):
  `cbuffer-large-fits-rootcbv-not-table`,
  `compute-dispatch-grid-shape-vs-quad`,
  `wavesize-attribute-missing`,
  `wavereadlaneat-constant-non-zero-portability`,
  `uav-srv-implicit-transition-assumed`.

**Estimated effort.** 2-3 weeks dev across the three parallel packs
after the shared-utilities PR (~3 dev days) lands. Doc-page authoring
parallelizes the same way — three pack-aligned doc-page batches.

Doc pages for these rules SHOULD be authored in parallel via the
standard `_template.md`; each carries the `Pre-v0 status` notice until
implementation lands.

---

## Phase 4 (control-flow / data-flow) implementation plan

**Scope.** Sixteen rules that need at least one of: a CFG over the
tree-sitter AST, a uniformity / wave-divergence analysis, a def-use
scan, or a barrier-aware reachability pass. Phase 4 infrastructure
(per ADR 0007 §Phase 4 + ADR 0010 §Phase 4) supplies all of the above.
Each new rule shares machinery with one already-locked rule — no
new analysis pass is introduced.

**LOCKED rules:**

*Groupshared / LDS micro-architecture:*

- `groupshared-stride-non-32-bank-conflict`: groupshared float arrays
  indexed `[tid*S+k]` for S in {2,4,8,16,64} hitting ≥2-way LDS bank
  serialization. *GPU reason:* RDNA 2/3 LDS and Turing/Ada shared
  memory both have 32 banks of 4 bytes; any stride sharing a non-
  trivial GCD with 32 still serializes accesses. Companion to the
  locked `groupshared-stride-32-bank-conflict`.
- `groupshared-dead-store`: write to a groupshared cell never read on
  any subsequent path before workgroup exit. *GPU reason:* wastes LDS
  bandwidth and pressures occupancy. Shares the def-use machinery the
  locked `unused-cbuffer-field` rule needs.
- `groupshared-overwrite-before-barrier`: groupshared cell written,
  then re-written by the same thread before any
  `GroupMemoryBarrier*`. *GPU reason:* the first write is unobservable
  to other threads and is pure waste.
- `groupshared-atomic-replaceable-by-wave`: `InterlockedAdd(gs[0], 1)`
  / `InterlockedOr(gs[0], mask)` where operands are wave-derivable
  and a `WaveActiveSum` / `WaveActiveBitOr` + a single representative-
  lane `InterlockedAdd` would replace 32-64 LDS atomics with one.
  *GPU reason:* drops atomic traffic by the wave size. Distinct from
  the locked `interlocked-bin-without-wave-prereduce` (small fixed-
  bin set); this targets single-counter accumulation.
- `groupshared-first-read-without-barrier`: read of `gs[expr]` before
  the first `GroupMemoryBarrierWithGroupSync` on any path where
  `expr` may resolve to a cell another thread writes. *GPU reason:*
  cross-lane race that occurs even when *some* thread has written
  before the barrier. Distinct from locked
  `groupshared-uninitialized-read` (any-thread, no-write case).

*Buffer divergence / coherence:*

- `divergent-buffer-index-on-uniform-resource`: `buf[i]` with
  divergent `i` on a buffer whose binding is uniform. *GPU reason:*
  Xe-HPG and Ada serialize divergent loads on the K$ / scalar cache.
  Shares uniformity machinery with the locked
  `wave-active-all-equal-precheck`.
- `rwbuffer-store-without-globallycoherent`: writes to a UAV later
  read on the same dispatch by a different wave without a barrier and
  without `globallycoherent`. *GPU reason:* without
  `globallycoherent` the L1 cache caches writes per-CU/SM and reads
  on a different unit see stale data.

*Mesh / amplification (CFG-bound):*

- `primcount-overrun-in-conditional-cf`: `SetMeshOutputCounts(v, p)`
  followed by primitive writes guarded by branches whose join
  produces > p primitives on some path. *GPU reason:* UB on
  RDNA/Ada/Xe-HPG. Companion to the locked
  `setmeshoutputcounts-in-divergent-cf` (call site, not writer side).
- `dispatchmesh-not-called`: amplification entry point with at least
  one CFG path that does not call `DispatchMesh`. *GPU reason:* UB;
  trivially observable from the CFG.

*Numerical / divergence:*

- `clip-from-non-uniform-cf`: `clip(x)` reachable from non-uniform CF
  in PS without `[earlydepthstencil]`. *GPU reason:* close to the
  locked `early-z-disabled-by-conditional-discard` but `clip()` has
  its own semantics distinct from `discard`; surfaces explicitly so
  the suppression scope is independent.
- `precise-missing-on-iterative-refine`: a Newton-Raphson / Halley
  iteration lacking `precise` qualifiers on the residual.
  *GPU reason:* fast-math reordering on Ada / RDNA / Xe-HPG can
  collapse the iteration to a no-op; analytic-derivative SDF /
  collision footgun.

*Wave / quad extras:*

- `manual-wave-reduction-pattern`: explicit `for` /
  `InterlockedAdd` / atomics that reproduce a `WaveActiveSum` /
  `WavePrefixSum`. *GPU reason:* saves 32-64 ALU ops + the LDS /
  atomic round-trip on every modern IHV.
- `quadany-quadall-opportunity`: `if (cond)` in PS where `cond` is
  per-lane and the branch body only executes derivative-bearing ops;
  could become `if (QuadAny(cond))` to keep helper-lane participation.
  *GPU reason:* companion (not duplicate) of the locked ADR 0010
  `quadany-replaceable-with-derivative-uniform-branch` — this is the
  *opposite* direction (replace plain `if` with `QuadAny`).
- `wave-prefix-sum-vs-scan-with-atomics`: hand-rolled compute-pass
  scan implemented with groupshared + barriers. *GPU reason:*
  `WavePrefixSum` + a single barrier collapses the multi-step scan
  on RDNA / Ada / Xe-HPG.

*Branch / divergence hints:*

- `flatten-on-uniform-branch`: `[flatten]` on an `if` whose condition
  is dynamically uniform. *GPU reason:* `[flatten]` forces both arms
  to execute on the cheap path; on uniform branches `[branch]` is
  the right choice and lets the compiler skip the inactive arm.
  Shares uniformity machinery with the locked
  `branch-on-uniform-missing-attribute`.
- `forcecase-missing-on-ps-switch`: `switch` in PS whose cases each
  contain texture sampling and that lacks `[forcecase]`.
  *GPU reason:* without `[forcecase]` the compiler may unroll the
  switch into chained `if`s, breaking quad-uniform sampling on RDNA
  / Ada.

**Pack split.** Mirroring ADR 0009's three-pack pattern, the 16
Phase 4 rules split into three thematic packs after one shared-
utilities PR. Each rule shares analysis with at least one already-
locked rule, so no new pass infrastructure is introduced — only new
pattern matchers on top of existing CFG / uniformity / def-use
infrastructure.

- **Shared-utilities PR (lands first).** Additions to the Phase 4
  data-flow utilities: `lds_def_use.hpp` (per-thread groupshared
  def-use scan), `barrier_reachability.hpp` (does this op reach this
  barrier on any path?), `branch_attr_uniformity.hpp` (does this
  branch attribute conflict with uniformity?). ~200 LOC + unit tests.
  Builds on the Phase 4 base infrastructure ADR 0007 / 0010 already
  scope.
- **PR A — groupshared-microarch-pack** (5 rules):
  `groupshared-stride-non-32-bank-conflict`, `groupshared-dead-store`,
  `groupshared-overwrite-before-barrier`,
  `groupshared-atomic-replaceable-by-wave`,
  `groupshared-first-read-without-barrier`.
- **PR B — divergence + coherence + mesh-pack** (6 rules):
  `divergent-buffer-index-on-uniform-resource`,
  `rwbuffer-store-without-globallycoherent`,
  `primcount-overrun-in-conditional-cf`, `dispatchmesh-not-called`,
  `clip-from-non-uniform-cf`,
  `precise-missing-on-iterative-refine`.
- **PR C — wave-quad-extras + branch-hints-pack** (5 rules):
  `manual-wave-reduction-pattern`,
  `quadany-quadall-opportunity`,
  `wave-prefix-sum-vs-scan-with-atomics`,
  `flatten-on-uniform-branch`,
  `forcecase-missing-on-ps-switch`.

**Estimated effort.** 3-4 weeks dev across the three parallel packs
after the shared-utilities PR (~4 dev days) lands. The
`groupshared-microarch-pack` is the schedule-critical path because
the LDS def-use scan touches the most surrounding code.

Doc pages for these rules SHOULD be authored in parallel via the
standard `_template.md`; each carries the `Pre-v0 status` notice until
implementation lands.

---

## Phase 5 (LSP) implementation plan

No Phase 5 additions from this ADR. The LSP / IDE work in Phase 5 is
ergonomics infrastructure (per ROADMAP "Phase 5"); none of the 63
candidates require a Phase 5-specific surface.

---

## Phase 7 (IR-level) implementation plan

**Scope.** Two rules that fundamentally need IR-level analysis:
register-pressure estimation and per-lane load aggregation. Same tier
as the existing Phase 7 `live-state-across-traceray` and the ADR 0010
`maybereorderthread-without-payload-shrink` — research-grade, gated on
real adoption.

**LOCKED rules:**

- `groupshared-when-registers-suffice`: groupshared backing for a
  per-thread temporary array of size ≤ N (configurable, default 8)
  that the compiler can keep in registers. *GPU reason:* every byte
  spent in LDS comes out of the occupancy budget; on RDNA 32 KB /
  NVIDIA 100 KB shared per CU/SM the marginal occupancy cliff is
  steep. Needs IR-level register-pressure estimation; same machinery
  as the existing Phase 7 `vgpr-pressure-warning`.
- `buffer-load-width-vs-cache-line`: scalar `Load` per lane that
  aggregates to a wave's worth of contiguous bytes that would
  coalesce with `Load4`. *GPU reason:* RDNA 64-byte cache line /
  Turing-Ada 128-byte L1 line want one wide transaction per wave;
  per-lane scalar loads burn extra request slots. Needs IR-level
  per-wave aggregation; ship alongside the existing
  `redundant-texture-sample` Phase 7 rule.

**Pack split.** Two rules; ship as one PR alongside the rest of the
Phase 7 IR-level pack. No parallel-pack split warranted.

**Estimated effort.** 1 dev week after the IR-reader infrastructure
lands. Schedule deliberately deferred to post-1.0 per the ROADMAP
Phase 7 stance ("These are research-grade and gated on real
adoption. Don't pre-build them.").

Doc pages for these rules SHOULD be authored in parallel via the
standard `_template.md`; each carries the `Pre-v0 status` notice until
implementation lands.

---

## Deferred candidates

Twenty candidates are DEFERRED — the GPU mechanism is real but the
detection pattern needs more research, the rule needs application-
level data the linter does not have, or the rule's value is too IHV-
specific to clear the portability bar. **Do not implement until follow-
up research lands** and a successor ADR (0011-A or higher) re-locks
them.

*Groupshared / LDS:*

- `groupshared-float64-bank-conflict` — NVIDIA-specific
  (Turing/Ada 32-bit banks); RDNA / Xe-HPG bank widths differ; needs
  an IHV-target gate before locking.
- `groupshared-aos-when-soa-pays` — heuristic-heavy; "wider than one
  bank" needs sharper definition + a confidence threshold before it
  produces tractable false-positive rates.
- `groupshared-non-pow2-size` — needs occupancy modelling per
  architecture (RDNA partition steps differ from NVIDIA / Xe-HPG);
  defer until the linter has a per-arch occupancy table.
- `groupshared-index-arith-defeats-coalescing` — pattern detection
  ("compiler can't prove monotonic in `tid`") is itself a compiler-
  internal property; defer until we have an IR-level monotonicity
  heuristic.
- `groupshared-after-early-return` — close enough to the locked
  `barrier-in-divergent-cf` and `early-z-disabled-by-conditional-
  discard` to risk double-firing; defer pending a clean separation
  spec.

*Buffer access:*

- `structured-buffer-aos-when-soa-pays` — engine-architecture
  refactor recommendation; needs whole-shader read-pattern analysis
  to avoid noisy fires on multi-field consumers.

*Sampler:*

- `mip-bias-default-on-streaming` — opt-in / project-policy rule by
  the brainstorm's own admission; defer until configuration surface
  for opt-in rules lands.

*Root signature (project-level):*

- `root-32bit-constant-pack-mismatch` — needs the application's root
  signature shape, not just shader reflection; defer until project-
  level root-signature input lands.
- `root-signature-shape-overflows-fast-path` — same — depends on
  IHV fast-path numeric thresholds the linter does not yet vendor.
- `root-constant-rebind-per-draw` — application-level (per-draw
  variability is invisible to the shader); defer until project-level
  draw-frequency input lands.

*Mesh extras:*

- `mesh-vertex-output-soa-mismatch` — "the implicit SoA expansion
  the driver expects" is vendor-specific (Ada / RDNA 3) and requires
  an IHV-target gate.
- `as-launch-grid-too-small` — `DispatchMesh(x,y,z)` arguments are
  often runtime; AST-only detection produces too many false negatives
  to be useful. Defer until cross-stage / runtime-arg flow lands.
- `as-payload-write-in-divergent-cf` — sufficiently close to the
  locked `setmeshoutputcounts-in-divergent-cf` that the suppression
  semantics need explicit design before locking.
- `payload-output-mismatch` — Slang / DXC already produce a hard
  PSO link error; the linter rule duplicates compiler output.
  Re-evaluate once we have an "explain compiler errors better"
  surface.

*Numerical:*

- `min16float-subnormal-flush-mismatch` — IHV-default-flush behaviour
  varies (RDNA / Ada flush; Xe-HPG preserves); needs an IHV-target
  gate and a flush-mode reflection accessor before locking.

*Texture format:*

- `format-bit-width-mismatch-on-load` — without an explicit conversion
  intent surface, the rule conflicts with valid format-narrowing
  patterns; defer until intent-annotation surface lands.

*Compute:*

- `numthreads-bad-for-1d-workload` — heuristic ("kernel reads `.x`
  only") fires on legitimate 2D-indexed kernels with `.y == 0`
  pattern; needs sharper detection.
- `numthreads-z-greater-than-one-without-3d-tid-use` — same heuristic
  family; defer with the prior rule.

*Resource state:*

- `transient-resource-not-aliased` — render-graph / placed-resource
  alias surface is application-side; defer until project-level memory
  graph input lands.

*Branch / divergence hints:*

- `branch-on-trivially-constant-cond` — needs cbuffer specialisation-
  constant analysis (Slang reflection exposes specialisation
  constants but the constant-fold pass through a cbuffer field is
  not yet wired); defer until specialisation-aware fold lands.

---

## Dropped candidates

Two candidates are DROPPED as duplicates of already-locked rules.

- `saturate-then-multiply-by-one` — strict subset of the locked
  ADR 0007 Phase 2 rule `mul-identity` (which already matches `_*1`).
  The brainstorm's "survives template / macro expansion" argument
  doesn't justify a separate rule; if `mul-identity`'s pattern needs
  to widen to catch macro-expanded forms, that's a refinement of the
  existing rule, not a new one. **Verdict: DROP.**
- `byteaddressbuffer-when-typed-load-suffices` — overlaps the LOCKED
  Phase 3 `byteaddressbuffer-narrow-when-typed-fits` (this ADR). The
  brainstorm describes the same mechanism (ByteAddressBuffer +
  `asfloat` round-trip vs. typed-cache path); the LOCKED rule
  subsumes both directions. **Verdict: DROP.**

### Adjudication of the three brainstorm-flagged near-duplicates

The brainstorm explicitly called out three candidates as *close to*
locked rules but distinct enough to warrant a separate rule. This
ADR confirms each:

- `groupshared-first-read-without-barrier` — **LOCKED Phase 4.** The
  brainstorm's distinction holds: locked
  `groupshared-uninitialized-read` is a no-thread-has-written
  property; this rule is a some-thread-has-written-but-not-before-
  the-barrier property. Cross-lane race semantics are different;
  suppression scopes are different. Lock as a sibling rule.
- `clip-from-non-uniform-cf` — **LOCKED Phase 4.** The locked
  `early-z-disabled-by-conditional-discard` matches `discard`;
  `clip()` has its own semantics (component-wise discard with a
  threshold) and authors expect to be able to suppress one without
  the other. Independent rule = independent suppression scope. Lock.
- `quadany-quadall-opportunity` — **LOCKED Phase 4.** The locked
  ADR 0010 `quadany-replaceable-with-derivative-uniform-branch`
  detects the *opposite* direction (`QuadAny` -> derivative-uniform
  branch). This rule detects the forward direction (`if` -> wrap in
  `QuadAny`). Symmetric pair, both worth shipping. Lock.

### Adjudication of one near-duplicate not flagged by the brainstorm

While auditing the candidate list against the locked rule set, this
ADR also identified `byteaddressbuffer-when-typed-load-suffices` as a
near-duplicate of `byteaddressbuffer-narrow-when-typed-fits` (both
describe the ByteAddressBuffer + `asfloat` pattern from different
angles; the cache-path argument is the same). Verdict: DROP the former
as redundant; the latter (LOCKED Phase 3) is the canonical rule.

The `saturate-then-multiply-by-one` candidate was flagged in the
brainstorm as a "companion" to `mul-identity` rather than a near-
duplicate. This ADR re-classifies it as a duplicate (not a companion)
and DROPs it; see the Dropped candidates section above.

## Consequences

**Good:**

- Adds 41 LOCKED rules to the roadmap, bringing the total locked count
  from ADR 0007 (41) + ADR 0010 (36) + the 3 already-shipped Phase 0/1
  rules + this ADR (41) = 121 rules across Phases 0-7 (the original
  ROADMAP `pow-const-squared` + ~50 first-draft rules count toward the
  pre-ADR 0007 baseline; this ADR's contribution is the +41 count).
- Each LOCKED rule has a clear phase placement, a one-paragraph GPU
  reason, and a parallel-pack-friendly implementation slot.
- The DEFERRED bucket records 20 candidates with explicit blockers, so
  the next research pass can pick them up without re-litigating the
  current state.
- The DROPPED bucket records 2 duplicate adjudications, so the same
  rules don't resurface in the next brainstorm.

**Bad:**

- Phase 3 grows by 17 rules and Phase 4 grows by 16. Reviewer load
  increases per phase. Mitigated by the per-phase parallel-pack split
  (3 packs per phase) and by the fact that every new rule shares
  analysis machinery with an already-locked rule (no new analysis pass
  introduced).
- Doc-page authoring load grows by 41 pages. Mitigated by the
  parallel-authoring stance: each LOCKED rule carries the standard
  `Pre-v0 status` notice, so doc pages can land before
  implementation.

**Per-pack blog-post obligation.** Per CLAUDE.md "Rule + blog post
pair" convention, every LOCKED rule lands with a companion blog post
under `docs/blog/` explaining the GPU mechanism. Phase 3's three packs
seed three thematic blog series (buffers + LDS-typed; samplers +
texture-format; root-sig + compute + wave-portability); Phase 4's
three packs seed three more (groupshared-microarch; divergence +
coherence + mesh; wave-quad-extras + branch-hints).

**Doc pages.** Each LOCKED rule needs a `docs/rules/<id>.md` page
following `_template.md`. Front-matter required: `rule_id`,
`category`, `phase`, `applicability`, `gpu_reason`. New categories
introduced by this ADR (proposed by the brainstorm and confirmed
here): `root-signature`, `texture-format`, `divergence-hints`,
`wave-quad-extras`, `precision`, `buffer-access`. CLAUDE.md's
category list updates to include these once the first rule in each
category lands.

**Test fixtures.** Each implementation needs corresponding fixtures
under `tests/fixtures/phaseN/<category>.hlsl` with `// HIT(rule-id)`
and `// SHOULD-NOT-HIT(rule-id)` annotations per the
`tests/fixtures/README.md` convention.

## More Information

- ADR 0007 (Rule-pack expansion, +41 rules, Accepted): the canonical
  exemplar for rule-pack-expansion ADRs; this ADR follows the same
  shape (verdict tally + per-phase distribution + brainstorm-
  methodology section embedded in the Decision Drivers).
- ADR 0009 (Phase 2 implementation plan, Proposed): the canonical
  exemplar for the per-phase implementation-plan structure
  (shared-utilities PR + parallel category packs); this ADR mirrors
  it for Phase 2 / Phase 3 / Phase 4 LOCKED additions.
- ADR 0010 (SM 6.9 rule expansion, +36 rules, Proposed): the canonical
  exemplar for a multi-phase Proposed ADR with experimental gating;
  this ADR follows the same per-rule-with-phase-tag table approach in
  the Decision Outcome.
- Brainstorm research date: **2026-05-01** (sourced from the
  ROADMAP "Candidate rule expansion" section, lines ~299-398).
- Doc pages for LOCKED rules can be authored in parallel since each
  carries the standard `Pre-v0 status` notice; the parallel-authoring
  stance matches the parallel-implementation stance.
- New rule categories introduced (proposed alongside this ADR's
  rules): `root-signature`, `texture-format`, `divergence-hints`,
  `wave-quad-extras`, `precision`, `buffer-access`. Each enters the
  CLAUDE.md "category list" once its first rule lands.
- Future expansions add an addendum ADR or a successor (this ADR is
  not edited after acceptance, per ADR 0007's precedent).
