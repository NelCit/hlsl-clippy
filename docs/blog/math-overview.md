---
title: "Where the cycles go: math intrinsics on modern GPUs"
date: 2026-05-01
author: NelCit
category: math
tags: [hlsl, shaders, performance, math, transcendentals, simd]
license: CC-BY-4.0
---

A pixel shader that calls `pow(x, 2.0)` once per fragment and a pixel shader
that calls `x * x` once per fragment do not cost the same number of cycles.
Most graphics engineers know this in the abstract. The exact ratio is the
useful number: in isolation, on RDNA3, the second form retires roughly nine
times faster than the first. Not 30 percent faster. Nine times. The
difference is not micro-optimisation theatre — it is the architectural gap
between two completely different execution units that happen to share an
HLSL intrinsic.

The math rule pack in `shader-clippy` is roughly thirty-one rules deep, and
every one of them earns its keep at exactly one of those gaps. This post is
the orientation map: four execution-unit distinctions, six thematic buckets,
and an honest account of where the compiler already saves you. The companion
post on [pow-const-squared](/rules/pow-const-squared) is the depth-first
version of one bucket; this post is the breadth-first version of all six.

## The GPU does not have one ALU

Every modern GPU has at least four arithmetic execution paths, and they do
not run at the same rate.

**The Vector ALU (VALU)** is the wide SIMD engine: SIMD32 on RDNA, the FP32
CUDA cores on NVIDIA, the XVE vector pipe on Intel Xe-HPG. It issues
`add`, `mul`, and `fma`/`mad` at one instruction per clock per lane. This
is the hot path. Architectures advertise their teraflops figures in terms
of VALU FMAs.

**The Special Function Unit (SFU)** — also called the Transcendental ALU
(TALU) on AMD docs, the Multi-Function Unit (MUFU) on NVIDIA, the
transcendental pipe on Intel — handles `sin`, `cos`, `exp2`, `log2`,
`rcp`, `rsqrt`, `sqrt`, and `pow`. It runs at quarter rate on every
shipping consumer architecture: the SFU within an RDNA2/3 CU processes one
quarter of the wave lanes per cycle, NVIDIA's Turing and Ampere SMs have a
4:1 ratio of FP32 cores to SFUs, and Intel Xe-HPG's transcendental pipe is
similarly narrower. A `v_log_f32` takes roughly four clocks to drain a
full RDNA wave; a `v_mul_f32` takes one.

**The integer ALU** is logically separate from the FP32 path on most
architectures and runs `popcount`, `firstbit`, bit shifts, and integer
multiplies at full rate. Its existence is why `countbits()` is basically
free and a hand-rolled SWAR popcount is twelve cycles of nothing.

**The LDS** (shared memory / scratchpad) is, for our purposes, the unit
lookup-table tricks accidentally end up on. Bank conflicts turn a
one-cycle table fetch into a serialised wave-wide stall.

The math rule pack is almost always telling you that you have written code
on the wrong unit. You promoted a full-rate VALU op into a quarter-rate SFU
op (every transcendental rule), or chained two SFU primitives where one
would do (`1.0 / sqrt(x)`), or routed an integer-ALU primitive through an
SFU detour (`(uint)log2((float)x)`), or hand-rolled an idiom that the
driver could have lowered to a single fused operation if you had let it
see the high-level intent.

## Transcendentals: pow, exp, log, sin, cos

The first bucket is the deepest because `pow` is everywhere in PBR shaders
and almost no spelling of it does what graphics programmers naively expect.

`pow(x, n)` does not compile to repeated multiplication. Every shipping HLSL
backend lowers it to `exp2(n * log2(x))` — two SFU instructions plus a
multiply. The exponent value is irrelevant to the cost: `pow(x, 2.0)`,
`pow(x, 3.0)`, `pow(x, 37.5)`, all the same instruction sequence, all
roughly nine clocks of SIMD latency at the wave level. The
[pow-to-mul](/rules/pow-to-mul) rule catches the small-integer cases
(2, 3, 4) where unrolling to multiplies replaces the SFU pair with one or
two VALU ops. [pow-const-squared](/rules/pow-const-squared) is the
focused-on-2 variant with the Schlick Fresnel motivating example.
[pow-integer-decomposition](/rules/pow-integer-decomposition) extends the
treatment to exponent 5 and above using exponentiation-by-squaring, where
`x^5` becomes `(x^2)^2 * x` — three multiplies, all VALU, no transcendental.

[pow-base-two-to-exp2](/rules/pow-base-two-to-exp2) catches the dual
direction: `pow(2.0, x)` algebraically simplifies to `exp2(x)`, but no
backend does the fold at HLSL level. The `log2(2.0)` is computed at runtime
on the SFU and then multiplied into `x`, doubling the SFU traffic for the
sub-expression. Rewriting to `exp2(x)` halves it. This shows up in
exponential fog, bloom attenuation, and HDR exposure curves — inner-loop
patterns in deferred and forward renderers.

The [sin-cos-pair](/rules/sin-cos-pair) rule is the one most graphics
engineers nod at instantly: if you compute `sin(theta)` and `cos(theta)` for
the same angle, the GPU has been able to emit a single fused operation since
SM 5.0. The HLSL intrinsic is `sincos(angle, s, c)`. The cost is not just
the second SFU issue — it is the angle-reduction step, the
`theta mod 2pi` mapping into the unit circle, which is the most expensive
part of any transcendental and which `sincos` shares across both outputs.
Rotation matrices in vertex shaders and angular-velocity updates in
particle systems hit this every dispatch.

## Reciprocal and square root: rsqrt is a primitive

The second bucket is the case where a single SFU instruction has been
replaced by two chained SFU instructions, and the fix is naming the
primitive that already exists.

`rsqrt(x)` is a hardware primitive on every shader-capable GPU. On RDNA it
is `v_rsq_f32`; on Xe-HPG it is `RSQ`. It is one quarter-rate SFU
instruction. The expression `1.0 / sqrt(x)` is a `sqrt` followed by an
`rcp` — two quarter-rate SFU instructions, roughly eight effective cycles
where four would do. [inv-sqrt-to-rsqrt](/rules/inv-sqrt-to-rsqrt) catches
the literal pattern, including the `rcp(sqrt(x))` and `1.0 / sqrt(dot(v,v))`
variants that come up in normalisation code.

The same argument applied to vector code is
[length-then-divide](/rules/length-then-divide). Manual `v / length(v)`
lowers to `v / sqrt(dot(v, v))`, where the divide is implemented as a
software macro on RDNA3 (an `rcp` plus a Newton-Raphson refinement step
totalling three to five effective cycles). The hand-rolled
`v * (1.0 / length(v))` rewrite is closer but still strictly worse than
`normalize(v)`, which compiles to the canonical `v * rsqrt(dot(v,v))` —
one SFU op and one full-rate vector multiply per component. Across a vertex
shader that normalises tangent, bitangent, and normal vectors per vertex,
halving the SFU traffic shows up directly in geometry-bound frame timings.

[length-comparison](/rules/length-comparison) is the same observation
applied at a comparison boundary. `length(v) < r` is equivalent to
`dot(v, v) < r * r` for any `r >= 0`, because `sqrt` is monotonic over
non-negative reals. The squared form replaces a quarter-rate `sqrt` with
one extra full-rate multiply. In particle collision detection and culling
shaders this single rewrite is the difference between fitting in the wave
occupancy budget and not.

## Built-in idioms: let the compiler see the intent

The third bucket is rules where the fix is not about cycle count
specifically — it is about giving the compiler a high-level semantic name
for an operation so the backend can lower it optimally. The instruction
counts of the manual and intrinsic forms are sometimes identical; the
intrinsic form scheduling is consistently better.

[manual-reflect](/rules/manual-reflect) catches the canonical
`v - 2.0 * dot(n, v) * n` formula in PBR shaders. The intrinsic
`reflect(v, n)` is defined to be exactly that expression, but the compiler
can lower a recognised `reflect` call to a tuned dependent-multiply-subtract
sequence with better latency hiding than the textually-decomposed form.
Cube-map IBL sampling and ray-tracing any-hit shaders are full of this
pattern.

[manual-distance](/rules/manual-distance) catches `length(a - b)` and
rewrites to `distance(a, b)`. Same instruction count, but `distance` makes
the no-aliasing relationship between the two named arguments visible to the
optimiser, enabling better scheduling — and it sets up
[length-comparison](/rules/length-comparison) for the further rewrite to
`dot(a-b, a-b) < r*r`.

[manual-smoothstep](/rules/manual-smoothstep) and
[manual-step](/rules/manual-step) catch the cubic Hermite polynomial
`t*t*(3-2*t)` and the binary-threshold ternary `x > a ? 1.0 : 0.0`
respectively. Both replace a hand-rolled sequence with the named intrinsic.
On RDNA, `step` lowers to `v_cmp_ge_f32` plus an implicit mask, sometimes
to a direct `SETGE`-class instruction that writes 0/1 to a VGPR without a
separate select. `smoothstep` enables the compiler to lift the
`1.0 / (edge1 - edge0)` reciprocal out of the wave when the edges are
uniform, a fold the manual form blocks. The deeper benefit is correctness:
the manual cubic Hermite is a place where typing `2.0` instead of `3.0` is
a silent shader bug; the intrinsic is verified by the compiler.

## Bit tricks: stop pretending floats are integers

The fourth bucket is the case where the integer ALU exists, has been there
since SM 5.0, and is being routed around for no reason.

[countbits-vs-manual-popcount](/rules/countbits-vs-manual-popcount) is the
canonical example. `countbits(x)` lowers to `v_bcnt_u32_b32` on RDNA — one
full-rate VALU op. A Brian-Kernighan loop runs at the maximum iteration
count of the wave (one lane with `0xFFFFFFFFu` makes the other 31 lanes
spin masked for 32 iterations). A SWAR popcount is branchless but still
twelve VALU instructions. A lookup-table popcount lives in a cbuffer
(fetch per byte processed) or LDS (bank conflicts when lanes index
different rows of the same bank). The intrinsic is twelve to thirty-two
times faster depending on the manual form.

[firstbit-vs-log2-trick](/rules/firstbit-vs-log2-trick) catches the
related sin: computing the most-significant set bit via `(uint)log2((float)x)`.
This routes a one-cycle integer op (`firstbithigh`, lowered to `v_ffbh_u32`
on RDNA) through the SFU detour: an int-to-float convert, a quarter-rate
`log2`, and a float-to-int truncation. Six to eight cycles versus one. The
asfloat-bit-trick variant skips the SFU but is silently wrong above 2^24
because the integer no longer round-trips through FP32, and produces
garbage for `x == 0` because `log2(0)` is `-inf`. The intrinsic is
defined to return `0xFFFFFFFFu` for zero — a checkable sentinel rather
than a NaN.

Light-clustering, BVH traversal stacks, occupancy compaction, and
mesh-shader visibility bitsets are the inner-loop hot paths where the
difference between a full-rate integer op and a quarter-rate transcendental
is directly measurable in shader nanoseconds.

## MAD and the fused multiply-add

The fifth bucket is about FMA scheduling, which is the foundation of every
GPU's advertised FP32 throughput.

[manual-mad-decomposition](/rules/manual-mad-decomposition) catches a
multiply pulled into a named temporary later added to something else:
`float scaled = t * scale; ... return scaled + bias;`. The fold to one
`v_fma_f32` (RDNA) or `FFMA` (NVIDIA) is reliable inside a single
expression — `t * scale + bias` — but breaks across a statement gap, an
intervening `if`, or when the intermediate is sunk into a struct field.
When the fold fails, the shader pays for two VALU instructions where one
would do, plus a cycle of extra VGPR liveness on RDNA. This is a suggestion
rather than an auto-fix because the split form is sometimes intentional
(debug prints, deliberate rounding boundaries — FMA's single-rounding can
change low-bit results in ways that have been validated against).

[select-vs-lerp-of-constant](/rules/select-vs-lerp-of-constant) is the same
issue at a higher abstraction. `lerp(K1, K2, t)` with both endpoints
constant is mathematically `mad(t, K2 - K1, K1)` — one FMA — but the fold
is fragile across SPIR-V and Metal back-ends and across `static const`
indirections, so the same source can compile to one VALU on D3D12 and two
on Vulkan. Writing the FMA directly in source removes the optimiser-luck
dependency.

[lerp-extremes](/rules/lerp-extremes) catches `lerp(a, b, 0.0)` and
`lerp(a, b, 1.0)` — the dead-arithmetic endpoints. The `b - a` and
multiply-by-zero are nominally dead but still occupy issue slots until a
constant-folding pass proves them dead, which does not always trigger
across function boundaries or after inlining. Material blending shaders
hitting fully-opaque or fully-transparent endpoints are common in practice.

## NaN-safe: the bug class that contaminates the frame

The sixth bucket is not about throughput. It is about correctness, and the
rules in it catch a class of bug that is one of the most common GPU
correctness incidents in production rendering code.

A single NaN pixel in a deferred buffer corrupts every subsequent
neighbourhood operation: TAA, denoisers, blur. NaN-poisoning manifests as
a black or fluorescent-pink dot that grows over a few frames. The hardware
does not flag the operation; the driver has no way to distinguish "this
NaN was a bug" from "this NaN was deliberate".

[acos-without-saturate](/rules/acos-without-saturate) catches the most
common production source: `acos(dot(normalize(a), normalize(b)))`. Two
vectors that are mathematically unit-length but whose `rsqrt` is
correctly-rounded only to within two ULP can have a dot product of
`1.0 + 5e-8`, and `acos` of an out-of-domain argument is NaN.
[sqrt-of-potentially-negative](/rules/sqrt-of-potentially-negative) catches
two-channel normal-map decoding (`sqrt(1.0 - dot(nxy, nxy))`), spherical
harmonics reconstruction, and ray-sphere discriminants — the dot-product
"unit length" drifts fractionally above 1, `1 - that` goes negative, `sqrt`
returns NaN. [div-without-epsilon](/rules/div-without-epsilon) catches
`(p2 - p1) / length(p2 - p1)` where `p1 == p2` produces inf, projections
where the projected-onto vector might be zero, and luminance divides where
the white point came from a `max` reduction that hit zero on a black frame.

The fix in all three cases is a `saturate`, `clamp`, or `max(x, eps)` wrap.
On RDNA and NVIDIA, `saturate` is a free output modifier on the producing
instruction; `clamp` and `max` are one VALU op. Zero or one cycle bought,
an entire NaN-poisoning bug class eliminated.

## What DXC catches and what it doesn't

DXC is a competent optimiser. It recognises `pow(x, 2.0)` for simple scalar
`x` and emits a single multiply. It folds `lerp(a, b, 0.0)` to `a` when both
operands are simple. It folds `length(a - b)` to `distance(a, b)`-equivalent
codegen on the DXIL backend at `-O3`. The math rule pack is not pretending
those fast paths do not exist.

Where the rules earn their keep is the cases the optimiser misses, and
those are not corner cases — they are the cases that actually appear in
production codebases:

- The argument to `pow` is a complex sub-expression; CSE decides
  duplicating it outweighs the transcendental saving and the fold does not
  fire.
- The function or expression is tagged `precise` for cross-platform
  determinism. Reassociation, strength reduction, and FMA fusion all back
  off to protect floating-point reproducibility. Engine codebases tag a lot
  of utility math `precise`.
- The shader is targeting SM 5.x via FXC, whose strength-reduction pass is
  older and less aggressive than DXC's.
- The fold has to propagate through a `static const` in a different
  translation unit, or behind an `inline` helper, and the optimiser loses
  track.
- The SPIR-V or Metal back-end does not replicate DXIL's specific folding
  heuristic; the same source compiles to one VALU on D3D12 and two on
  Vulkan or Metal.
- Vector literals — `lerp(float3(...), float3(...), t)` — where partial
  folding leaves one component eliminated and the others as full sub+mad
  sequences.
- Debug builds. `-Od` and `/Od` ship in more development workflows than
  anyone admits, and many of these rules fire there exclusively.

The other reason to run the lint even when you trust DXC: the NaN-safe
bucket is not about performance at all. It is about a bug class that
happens regardless of optimisation level.

## Where to next

The full math rule catalogue is at
[rules/?category=math](/rules/?category=math). Each rule page has the
detection pattern, the GPU-mechanism rationale, before-and-after examples,
and a link to the relevant ISA documentation. The depth-first companion
post for the squared case lives at
[pow-const-squared](/rules/pow-const-squared).

The math pack is the largest single category in `shader-clippy` because the
HLSL math intrinsic surface is the largest single surface where syntax and
ISA cost diverge. Profile first, fix what the profiler points at, and let
the linter calibrate where to look.

---

`shader-clippy` is an open-source HLSL linter. Rules, issues, and discussion
live at [github.com/NelCit/shader-clippy](https://github.com/NelCit/shader-clippy).
If you have encountered a shader pattern that should be a lint rule, open an
issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
