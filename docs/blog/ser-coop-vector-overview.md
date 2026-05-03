---
title: "SM 6.9: shader execution reordering, cooperative vectors, and the new ray-tracing primitives"
date: 2026-05-01
author: NelCit
category: sm69
tags: [hlsl, shaders, performance, sm-6-9, ser, cooperative-vectors, long-vectors, opacity-micromaps, ray-tracing]
license: CC-BY-4.0
---

A pathological raygen shader on RTX 4090: every lane builds a primary ray,
calls `TraceRay` against a BVH that fans out to thirty-two distinct
closest-hit shaders depending on material. The wave issues a single trace,
the RT cores process all 32 rays in parallel, and the SM then has to
dispatch thirty-two different shaders across thirty-two lanes that no
longer share an instruction stream. The wave executes the union of every
shader's instructions, masking off all but one lane at each step. On Ada
Lovelace that pattern measures within a factor of two of completely
serial execution. Replace the trace with the SER form —
`dx::HitObject::TraceRay` + `dx::MaybeReorderThread` keyed on a material
ID — and the same workload runs roughly an order of magnitude faster,
because the runtime physically rearranges lanes so each wave contains 32
lanes that all dispatch into the same hit group. NVIDIA's case studies on
Indiana Jones and Cyberpunk path tracing report 1.4-2x end-to-end gains
from this rearrangement alone.

That single optimization is the marquee feature of Shader Model 6.9. It is
not the only one. SM 6.9 — landed in DXIL 1.9, exposed in retail through
Agility SDK 1.619 (February 2026) — adds four new portable surfaces that
collectively reshape how a modern HLSL author writes a ray-traced or
ML-augmented frame: Shader Execution Reordering (SER), Cooperative Vectors,
Long Vectors (`vector<T, N>` for `5 <= N <= 1024`), and Opacity Micromaps
(OMM) in DXR 1.2. Each surface ships its own footguns. This post is the
overview for the twenty-three `shader-clippy` rules that catch them.

## What SM 6.9 actually adds

SM 6.9 is a step change because it surfaces hardware that has been
sitting on every modern GPU — the tensor / matrix engines, the RT core's
pre-dispatch reordering hooks, the wider register-file paths — to HLSL
for the first time without vendor extensions.

- **Shader Execution Reordering (SER)** introduces `dx::HitObject` and
  `dx::MaybeReorderThread`. A `HitObject` is a deferred ray-tracing hit
  that has executed traversal but has not yet been dispatched to its
  closest-hit / any-hit / miss shader. The `MaybeReorderThread` intrinsic
  asks the runtime to permute lanes before that dispatch so that lanes
  with similar work (same hit group, same material, same coherence
  bucket) end up in the same wave. The launch hardware is NVIDIA Ada
  Lovelace; AMD's RDNA 4 forward-path documentation describes a
  compatible mechanism, and the Vulkan analog
  (`VK_EXT_ray_tracing_invocation_reorder`) ships for both vendors.

- **Cooperative Vectors** introduces `dx::linalg::MatrixMul`,
  `MatrixVectorMul`, and `OuterProductAccumulate`: HLSL primitives that
  lower to the matrix-multiply hardware on every modern GPU. Ada
  Lovelace and Blackwell map these onto tensor cores; RDNA 3/4 maps
  them onto WMMA; Intel Xe2 maps them onto XMX. Before SM 6.9, calling
  this hardware required vendor extensions or DirectML.

- **Long Vectors** (HLSL proposal 0030, "DXIL vectors") extend
  `vector<T, N>` from the legacy 1/2/3/4 widths up to 1024 elements. The
  point is codegen: DXIL gains first-class wide-vector types, and the
  IHV scalarizer expands them onto the natural wave shape — packed-FP16
  on Ada, wave-aligned register pairs on RDNA 3, SIMD16/SIMD32 on Xe2.

- **Opacity Micromaps (OMM)**, formally a DXR 1.2 feature riding the SM
  6.9 release, adds per-triangle opacity tables that the BVH traversal
  hardware can consult without invoking an any-hit shader. The
  hardware launches were NVIDIA Ada Lovelace (where OMM debuted in
  vendor-extension form) and AMD RDNA 4; Intel Xe2 supports it through
  the OMM extension.

Each surface has a corresponding rule sub-category in `shader-clippy`.
Twenty-three rules ship in v0.5 across these four areas. The sections
below walk each surface, picking the rules that best illustrate the
hardware reasoning.

## Shader Execution Reordering: ten rules, three categories of mistake

SER is the densest rule pack in this release because the SER programming
model has the most hard constraints. A `dx::HitObject` is not an ordinary
value type — it represents per-lane RT-core state that the runtime owns
jointly with the SM. Three categories of mistake show up: lifetime
violations the spec marks as undefined behaviour, coherence-hint mistakes
that confuse the scheduler's bucketing, and missed opportunities where
the application paid for the HitObject machinery without recovering it
through a reorder.

### Lifetime violations

[`hitobject-stored-in-memory`](/rules/hitobject-stored-in-memory) catches
the most fundamental error: writing a `dx::HitObject` to `groupshared`,
to a UAV, to a return slot of a non-inlined function, or to any
non-register lifetime. On Ada the HitObject lives in a per-lane register
file slice that the RT cores own jointly with the SM; on RDNA 4 the same
lifetime constraint applies. There is no canonical memory layout because
storing the value would force the runtime to spill the entire RT-core
scoreboard to VRAM and refill it on every load, which would obliterate
the perf advantage SER exists to deliver. The spec marks this as UB; DXC
errors; the lint surfaces a source-located diagnostic before the build
breaks.

[`maybereorderthread-outside-raygen`](/rules/maybereorderthread-outside-raygen)
catches the closely-related stage error. SER coalescing has to happen
before the per-lane work it coalesces, which means the reorder
intrinsic is restricted to raygen. Once a wave has dispatched into a
closest-hit shader, the lane mapping is committed; reordering at that
point would force the runtime to spill the wave and re-form it, which
costs more than the reorder saves.
[`hitobject-construct-outside-allowed-stages`](/rules/hitobject-construct-outside-allowed-stages)
covers the construction analog.

[`hitobject-passed-to-non-inlined-fn`](/rules/hitobject-passed-to-non-inlined-fn)
is the one that requires inter-procedural reasoning. The SER spec
restricts HitObject lifetimes to inlined call chains because the
runtime's per-lane state cannot survive a function-call boundary that
spills to memory. The Phase 4 call-graph walk catches the cases where a
HitObject crosses into a function the compiler has not (or cannot)
inline.
[`hitobject-invoke-after-recursion-cap`](/rules/hitobject-invoke-after-recursion-cap)
guards the recursion budget: invoking a HitObject counts against the
PSO's `MaxTraceRecursionDepth`, and the runtime rejects PSOs that
overcommit.

### Coherence-hint mistakes

The `dx::MaybeReorderThread(hit, hint, hintBits)` overload takes a
user-supplied coherence hint that augments the scheduler's intrinsic
bucketing. The trap is that the scheduler already knows about the
HitObject's hit-group, shader-table-index, and miss-vs-hit axes;
re-encoding any of those into the hint duplicates work and forces a
worse final grouping than the no-hint baseline.

[`coherence-hint-encodes-shader-type`](/rules/coherence-hint-encodes-shader-type)
runs a Phase 4 taint analysis from `hit.IsHit()` and
`hit.GetShaderTableIndex()` returns through arithmetic and conditional
expressions, firing when their values reach the hint argument. Both
NVIDIA's SER perf blog and the Indiana Jones case study call this out:
pick a hint the driver does not already know — material ID, payload
bucket, BVH instance — and the bucketing wins decisively.
[`coherence-hint-redundant-bits`](/rules/coherence-hint-redundant-bits)
catches the simpler case where the hint expression contains bits the
`hintBits` parameter is masking off anyway.

### Missed opportunities and synchronisation

[`ser-trace-then-invoke-without-reorder`](/rules/ser-trace-then-invoke-without-reorder)
is the missed-opportunity classic. Constructing a HitObject and
immediately invoking it without an intervening `MaybeReorderThread`
means the application paid the HitObject's small fixed overhead — every
IHV's RT path has one — without ever recovering it through a reorder.
Plain `TraceRay` is strictly cheaper in that case. The Phase 4
reachability analysis walks from each construction site to its
invocation site and verifies that no `MaybeReorderThread` exists on any
path.

[`reordercoherent-uav-missing-barrier`](/rules/reordercoherent-uav-missing-barrier)
is the silent-corruption rule. A UAV qualified `[reordercoherent]`
promises the runtime that the application has placed explicit
synchronisation around the reorder point; missing that barrier means a
write from "old lane 5" and a read from "new lane 5" reference
unrelated data, but the L1 cache happily returns the old SM's stale
entry. The failure mode is wrong pixels or NaN propagation, with no
driver diagnostic.
[`fromrayquery-invoke-without-shader-table`](/rules/fromrayquery-invoke-without-shader-table)
catches the related definite-assignment bug where a HitObject
constructed via `FromRayQuery` is invoked without
`SetShaderTableIndex` having been called on every reaching path.

## Cooperative vectors: laying out the matrix the engine wants

Cooperative vectors expose the matrix-multiply hardware on every modern
GPU. The performance trap is layout. NVIDIA tensor cores want one
weight layout; AMD WMMA wants another; Intel XMX wants a third. The
HLSL spec exposes two opaque enums —
`MATRIX_LAYOUT_INFERENCING_OPTIMAL` and `MATRIX_LAYOUT_TRAINING_OPTIMAL`
— that the driver maps to its hardware-preferred layout at upload time.
Using a generic row-major or column-major layout works, but the matrix
engine has to perform a per-element transpose on every fetch, which
trashes the throughput the engine exists to provide.

[`coopvec-non-optimal-matrix-layout`](/rules/coopvec-non-optimal-matrix-layout)
fires on any `MatrixMul` / `MatrixVectorMul` /
`OuterProductAccumulate` call whose layout argument is not one of the
optimal enums. NVIDIA's published cooperative-vector blog cites 2-4x
speedups on inference-tier matmuls when the optimal layout is used vs.
the generic row-major path; AMD RDNA 3/4 and Intel Xe2 documentation
cite similar magnitudes. The conversion is paid once at upload time
through `D3D12_LINEAR_ALGEBRA_MATRIX_LAYOUT_CONVERT`, so the optimal
path wins decisively for any matrix used more than once.

[`coopvec-fp8-with-non-optimal-layout`](/rules/coopvec-fp8-with-non-optimal-layout)
upgrades the warning to an error for the FP8 case: the FP8 codepath has
no fallback for non-optimal layouts and the spec marks it as undefined
behaviour rather than slow.
[`coopvec-stride-mismatch`](/rules/coopvec-stride-mismatch),
[`coopvec-base-offset-misaligned`](/rules/coopvec-base-offset-misaligned),
and
[`coopvec-transpose-without-feature-check`](/rules/coopvec-transpose-without-feature-check)
catch the related per-call validation errors: the stride must match the
matrix dimensions and element type, the base offset must be aligned to
the engine's preferred granularity, and the transpose-on-load path must
be guarded by a runtime feature check because not every IHV exposes it
at the same SM level.

[`coopvec-non-uniform-matrix-handle`](/rules/coopvec-non-uniform-matrix-handle)
is the Phase 4 uniformity rule. The matrix engine on every supporting
IHV executes one matmul per wave, drawing operands from one source
matrix per call. When any of the matrix-handle / offset / stride /
interpretation arguments is divergent across the wave, the engine
cannot service the call as a single matmul; the driver serialises by
re-issuing the matmul once per unique argument tuple. NVIDIA's
cooperative-vector blog cites a 4-32x cost cliff when the matrix
handle is divergent. Hoist the argument out of any divergent branch
and the cliff goes away.

## Long vectors: where the wide-codegen path narrows

Long vectors are deceptively simple. `vector<float, 16>` looks like a
generalisation of `float4`. The catch is that DXIL only accepts wide
vectors in places where the legacy 1-4 widths can be lowered the same
way: elementwise arithmetic, unary intrinsics, comparison ops. The
moment a long vector lands somewhere with structural intent — a cbuffer
member, an inter-stage IO slot, a typed-buffer load — the lowering
falls off a cliff and the compiler errors.

[`long-vector-non-elementwise-intrinsic`](/rules/long-vector-non-elementwise-intrinsic)
fires on `cross`, `length`, `normalize`, `dot` (under specific arity
rules), `transpose`, `mul`-with-matrix, and `determinant` applied to
long vectors. `cross` is geometrically defined only for 3-vectors;
`length` is `sqrt(dot(v, v))` and the `dot` reduction tree is a
structural operation the long-vector lowering does not implement;
`mul(M, v)` is matrix-vector multiply with a different cost model
(cooperative vectors are the right surface for that). The rule is pure
AST and ships in Phase 2.

[`long-vector-in-cbuffer-or-signature`](/rules/long-vector-in-cbuffer-or-signature)
catches the layout-boundary error. Cbuffer packing rules predate the
long-vector feature: members are packed into 16-byte slots and a
`vector<float, 8>` member crosses two slots in a way the rules do not
enumerate. Inter-stage IO is packed into per-vertex slots that vary by
IHV — Ada has 32 four-component slots, RDNA 2/3 uses parameter-cache
entries, Xe-HPG uses URB-style slots — and none of them accommodate a
wide-vector type. The fix is to split the long vector into chunks of 4.

[`long-vector-typed-buffer-load`](/rules/long-vector-typed-buffer-load)
catches the related resource-type error: typed buffers
(`Buffer<float4>`) are limited to four-element loads by the resource
descriptor, regardless of what the in-shader type says. Move to a
`ByteAddressBuffer` and load the wide vector explicitly.
[`long-vector-bytebuf-load-misaligned`](/rules/long-vector-bytebuf-load-misaligned)
then catches the constant-offset alignment trap: a wide-vector load
from a `ByteAddressBuffer` at a non-multiple-of-the-vector-stride
offset either falls back to a byte-by-byte path or, on stricter
implementations, returns garbage.

## Opacity Micromaps: the gate-pair trap

OMM is the smallest pack — three rules — but every one of them is a
hard validation rule because the OMM specification has a gate-pair
structure that is easy to half-set. To consult the OMM blocks during
BVH traversal, the application must set both a per-trace ray flag
(`RAY_FLAG_ALLOW_OPACITY_MICROMAPS`) and a pipeline-state-object flag
(`D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS`). The
two-state collapse flag (`RAY_FLAG_FORCE_OMM_2_STATE`) further requires
that OMM is allowed in the first place. Any combination that sets one
gate without the matching counterpart is undefined behaviour.

[`omm-rayquery-force-2state-without-allow-flag`](/rules/omm-rayquery-force-2state-without-allow-flag)
catches the simplest version: a `RayQuery<RAY_FLAG_FORCE_OMM_2_STATE>`
instantiation that does not also set the allow flag. The two-state
collapse turns OMM's four states (opaque, transparent,
unknown-opaque, unknown-transparent) into two, which is meaningful
only when OMM is consulted at all. The fix is a single OR.

[`omm-allocaterayquery2-non-const-flags`](/rules/omm-allocaterayquery2-non-const-flags)
catches the constant-fold violation: `RayQuery2`'s second template
parameter is a compile-time `RAY_FLAG_*` value, not a runtime variable.
DXC errors on the simplest forms; the lint catches the rest by walking
the constant-fold chain.
[`omm-traceray-force-omm-2state-without-pipeline-flag`](/rules/omm-traceray-force-omm-2state-without-pipeline-flag)
is the project-level version of the gate-pair rule. The pipeline flag
is set application-side in the
`D3D12_RAYTRACING_PIPELINE_CONFIG1` subobject; reading it requires
Slang reflection over the state-object subobjects. The lint catches the
case where the per-trace flag is set but the pipeline flag is missing,
which is the mismatch DXC's per-shader validator cannot see.

## Where this is going

Three SM 6.9 directions are partially covered today and queued for full
coverage in subsequent rule packs (per ADR 0010):

- **Mesh nodes in work graphs** are SM 6.9 preview, not retail. Three
  rules — `mesh-node-not-leaf`, `mesh-node-missing-output-topology`,
  `mesh-node-uses-vertex-shader-pipeline` — ship gated behind
  `[experimental] work-graph-mesh-nodes = true` in `.shader-clippy.toml`.
  When the preview API in Agility SDK locks, the gate goes away.

- **`maybereorderthread-without-payload-shrink`** is queued for Phase 7
  because it requires IR-level live-range analysis at the reorder
  call site to determine the smallest sufficient payload. NVIDIA's
  Indiana Jones case study credits payload shrinking around the reorder
  with a substantial fraction of the SER win; the rule lands when the
  Phase 7 IR infrastructure does, alongside the related
  `live-state-across-traceray` rule.

- **SM 6.9 numerical specials** — `isspecialfloat`, `isnormal`, FP16
  promotion behaviour around the new intrinsics — are partially covered
  in the SM 6.9 numerical pack.

Cooperative vectors and long vectors are likely to grow as engines
adopt them in production: the rules in v0.5 cover the hardware-
specified validation surface, but the perf surface (LDS pressure from
long-vector temporaries; register pressure from wide cooperative-vector
accumulators) will grow as profilers report measured costs. Issues and
PRs are the channel.

## Run it on your raygen shader today

If your engine has a path-tracing path, an inference path, or a wide-
vector reduction, `shader-clippy` v0.5 covers the SM 6.9 surface from day
one. The rules ship default-on for retail SM 6.9 features (SER,
cooperative vectors, long vectors, OMM) and gated for the preview
surface (mesh nodes). Per-line suppression and per-rule severity
overrides work the same as on the rest of the rule pack:

```hlsl
dx::HitObject hit = dx::HitObject::TraceRay(/*...*/);
hit.Invoke(payload); // shader-clippy: allow(ser-trace-then-invoke-without-reorder)
```

Or, in `.shader-clippy.toml`:

```toml
[rules]
ser-trace-then-invoke-without-reorder = "warn"
coopvec-non-optimal-matrix-layout     = "error"
long-vector-typed-buffer-load         = "error"

[experimental]
work-graph-mesh-nodes = false
```

The companion blog posts for the individual rules go deeper on each
mechanism — the per-IHV layout details for cooperative-vector matrices,
the call-graph reasoning behind the HitObject lifetime rules, the
constant-fold mechanics for the OMM gate pair. Read whichever one your
profiler points you at.

---

`shader-clippy` is open source. Rules, issues, and discussion live at
[github.com/NelCit/shader-clippy](https://github.com/NelCit/shader-clippy).
If you have encountered a shader pattern that should be a lint rule,
open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
