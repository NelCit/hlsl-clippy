---
title: "Helper lanes are real lanes, and your wave intrinsics are reading them"
date: 2026-05-01
author: NelCit
category: wave-helper-lane
tags: [hlsl, shaders, performance, wave, helper-lane, pixel-shader, ps]
license: CC-BY-4.0
---

A pixel shader on a triangle edge runs on a 2x2 quad where only the
top-left lane is covered. The other three are helpers — same code
path, same registers, same fetches, only the framebuffer write is
masked off. Halfway through, the author calls
`WaveActiveSum(brightness)` for an exposure-feedback path; the
result feeds a `Sample` call's UV offset. The helpers get back zero,
because they are excluded from the wave's active mask by default.
Three of the four quad lanes now disagree about the UV. The hardware
computes derivatives across the quad anyway, picks a mip from the
garbage gradient, and returns texels from the wrong frequency band.
The frame ships with sparkle on every alpha-tested edge.

The shader compiles clean. DXC says nothing. The validator says
nothing. RGA, Nsight, and PIX all show "wave intrinsics in pixel
shader" and walk away. The bug lives in the gap between two SM 6.7
features that interact in a way the compiler does not flag:
helper-lane participation in wave ops, and derivative coherence
across the quad.

This post is about the wave-helper-lane category: a thin pack in v0.5
that targets that specific gap. We have **two rules shipped** here
and **five more queued as doc-only stubs** awaiting the SM 6.7 / 6.8
validator surface or the Phase 4 control-flow infrastructure. The
scope is narrower than math or workgroup, but the rules that *are*
shipped catch a class of bug that everything else misses.

The deeper SIMT primer — wave / warp model, divergence, predication —
lives in the
[control-flow category overview](/blog/control-flow-overview). Read
that first if any of the terms above are new. This post assumes that
model and goes one layer deeper into the helper-lane / wave-op
contract.

## Helper lanes, briefly: why fragments outside the primitive still execute

Pixel shaders execute on 2x2 quads because derivatives need them.
`ddx(uv)` reads `uv` from the two lanes on the same row of the quad
and subtracts; `ddy(uv)` reads from the same column.
`Texture.Sample` without an explicit gradient or LOD does this
implicitly — it reads `uv` from all four quad lanes, forms gradients,
picks a mip level, and only then fetches.

When the rasteriser covers only part of the quad — typical on edges,
silhouettes, and small triangles — the uncovered lanes are launched
anyway as **helper lanes**: they execute every instruction, hold
valid register state, participate in derivative computation, and have
their framebuffer writes suppressed. They are not free. On AMD RDNA 2
/ RDNA 3 they consume a slot in the wave32 or wave64 EXEC mask. On
NVIDIA Turing, Ampere, Ada, and Blackwell they consume a lane in the
warp's 32-wide active mask. On Intel Xe-HPG they consume a channel
in the SIMD16 EMASK. Every cycle the active lanes spend, the helpers
spend too.

By default, **wave intrinsics in pixel shaders exclude helper lanes
from the active mask.** A `WaveActiveSum(x)` on a partially-covered
quad sums only the covered lanes; the helpers contribute nothing and
receive an undefined value back. This is usually what authors want —
helper-lane data is rarely meaningful for non-derivative work — and
the semantics are documented in the SM 6.0 wave-intrinsic spec.

The trap is what happens when the wave-op result *flows into a
derivative-bearing operation*. If the four quad lanes disagree about
a value because the helpers got a different reduction result than the
covered lanes, the derivative is contaminated and the implicit-LOD
`Sample` picks the wrong mip. SM 6.7's
`[WaveOpsIncludeHelperLanes]` attribute lets authors opt back in to
including helpers when this dataflow happens. The Phase 4 data-flow
analysis catches the missing-attribute case.

## Shipped rule: wave-reduction-pixel-without-helper-attribute

The opening anecdote is exactly this rule.
[wave-reduction-pixel-without-helper-attribute](/rules/wave-reduction-pixel-without-helper-attribute)
fires on a pixel-shader entry that performs a wave reduction
(`WaveActiveSum`, `WaveActiveProduct`, `WaveActiveCountBits`,
`WaveActiveBallot`, and the `WavePrefix*` family) whose result then
flows into `ddx`, `ddy`, or an implicit-derivative `Sample`. The Phase
4 data-flow pass traces the reduction's output through assignments and
arithmetic until it either reaches a derivative-bearing op (rule
fires) or escapes the function (rule stays quiet).

The fix is a one-line attribute on the entry:

```hlsl
[WaveOpsIncludeHelperLanes]
float4 main(float2 uv : TEXCOORD0) : SV_Target0 {
    float waveAvg = WaveActiveSum(uv.x) / WaveActiveCountBits(true);
    float dudx    = ddx(waveAvg);
    return g_Albedo.Sample(g_Sampler, uv + float2(dudx, 0));
}
```

With the attribute, the helper lanes participate in the reduction and
all four quad lanes see the same `waveAvg`, so `ddx(waveAvg)` is zero
(or close to it) by construction and the implicit `Sample` picks a
sane mip. The rule is suggestion-tier rather than error-tier because
some shaders deliberately want approximate derivatives for cheap
debug-visualisation paths; the diagnostic emits the candidate
attribute as a comment and asks the author to confirm.

The architectural note is that this is a pixel-shader phenomenon
exclusively. NVIDIA Turing onwards delivers wave intrinsics through
the SM's warp-shuffle path with helper lanes explicitly tracked in
the active mask. AMD RDNA 2 / RDNA 3 implements the same model with
the EXEC mask plus a separate per-lane helper-status bit that wave-op
encoders consult. Intel Xe-HPG is identical at the conceptual level.
None of the three IHVs deviate from "exclude helpers by default", so
the bug reproduces identically across vendors — which is precisely
why a portable linter is the right place for it.

## Shipped rule: quadany-replaceable-with-derivative-uniform-branch

The second shipped rule attacks the inverse waste:
[quadany-replaceable-with-derivative-uniform-branch](/rules/quadany-replaceable-with-derivative-uniform-branch)
catches `if (QuadAny(cond))` wrappers that pay for a wave shuffle
they do not need.

`QuadAny` and `QuadAll` (SM 6.7) are the canonical guards for keeping
helper-lane participation alive across a per-lane branch. When any
lane in the quad wants to enter a `Sample`-bearing arm, all four
should enter, because dropping the helpers leaves the survivors
without derivative neighbours. The `QuadAny`-then-`if` idiom does
exactly that — it forces all four lanes to take the branch when any
of them wants to, so the implicit-LOD sampler inside still gets a
valid quad.

The cost is the wave shuffle. On RDNA 2 / RDNA 3 the quad intrinsics
lower to `ds_swizzle_b32` plus a small reduction — two to four
instructions. On NVIDIA Turing+ they lower to `SHFL.IDX` with a
quad-mask plus a vote — similar order. On Intel Xe-HPG SIMD16 the
quad lanes are co-located in one SIMD group and the intrinsic is
roughly two cycles. None catastrophic, none zero — and an engine
with `QuadAny` sprinkled defensively across every gradient-bearing
branch pays the cost hundreds of times per pixel.

The rule fires when the body of the `if` is **derivative-uniform** —
every operation inside either uses no derivatives or operates on
values that are constant across the quad. The classic example is a
sample with a quad-uniform UV pulled from a constant buffer:

```hlsl
if (QuadAny(uv.y > 0.5)) {                    // wrapper redundant
    c = g_Atlas.Sample(g_Sampler, g_AtlasUV);  // body uses uniform UV
}
```

Because `g_AtlasUV` is identical on every lane, the implicit
derivative is zero whether helpers participate or not. The `QuadAny`
adds nothing. Drop it:

```hlsl
if (uv.y > 0.5) {
    c = g_Atlas.Sample(g_Sampler, g_AtlasUV);
}
```

The Phase 4 branch-shape detector identifies the wrapper; data-flow
verifies the body has no derivative dependence on quad-divergent
values. The proof is approximate, so the rule is suggestion-tier — a
complex body where uniformity is hard to confirm keeps the wrapper.

## What is queued in this pack

The shipped surface is two rules. ADRs 0010 and 0011 also locked
five additional rules in this category that ship as doc-only stubs in
v0.5 because they need infrastructure that lands later. They are
documented so contributors can see what is in flight; the companion
.cpp files do not yet exist.

**[wavesize-range-disordered](/rules/wavesize-range-disordered)** —
constant-folds the integer arguments to `[WaveSize(min, max)]` and
`[WaveSize(min, preferred, max)]` (SM 6.8) and fires when the
ordering is violated. `[WaveSize(64, 32)]` is a hard PSO-creation
failure on every IHV; catching it at lint time gives a source-located
fix instead of an opaque runtime reject. AST-only.

**[wavesize-fixed-on-sm68-target](/rules/wavesize-fixed-on-sm68-target)** —
fires on a fixed `[WaveSize(N)]` against an SM 6.8+ target where the
range form is strictly more flexible. RDNA 1 / 2 / 3 can run wave32
or wave64 by driver heuristic; pinning to one makes the kernel
unrunnable when the other is preferred. NVIDIA Turing onwards is
fixed wave32 so `[WaveSize(64)]` is unrunnable there. Xe-HPG runs
wave8 / 16 / 32 by register pressure. Needs Slang reflection for the
target SM; gated on Phase 3.

**[waveops-include-helper-lanes-on-non-pixel](/rules/waveops-include-helper-lanes-on-non-pixel)** —
fires on `[WaveOpsIncludeHelperLanes]` applied to a non-pixel entry.
There are no helpers outside PS; the attribute is meaningless on
compute and a hard validator error in some toolchains. Phase 3.

**[quadany-quadall-non-quad-stage](/rules/quadany-quadall-non-quad-stage)** —
fires on `QuadAny`, `QuadAll`, and `QuadReadAcross*` from any stage
without quad semantics: vertex, geometry, raytracing, mesh,
amplification, and compute with `[numthreads]` shapes that do not
produce well-formed 2x2 groups. The interesting catch is the
compute-with-bad-numthreads case where the stage is legal but the
thread-shape is not. Phase 3.

**[startvertexlocation-not-vs-input](/rules/startvertexlocation-not-vs-input)** —
fires on `SV_StartVertexLocation` (and `SV_StartInstanceLocation`)
used anywhere other than as a VS input. The SM 6.8 semantic is the
per-draw `BaseVertexLocation` constant the rasteriser broadcasts; it
has no defined meaning at later stages because the inter-stage
parameter cache does not propagate it. Phase 3.

ADRs 0010 and 0013 lay out the SM 6.7 / 6.8 surface and the Phase 4
infrastructure that gates these; expect the queued five across v0.6
and v0.7 as the validator and CFG layers fill in.

## See also

The big picture for SIMT-aware lint rules — wave / warp model,
divergent branches, derivative-in-divergent-CF, barriers in divergent
CF, and the `wave-intrinsic-non-uniform` /
`wave-intrinsic-helper-lane-hazard` / `discard-then-work` triad — is
in the
[control-flow category overview](/blog/control-flow-overview). Those
three rules live in control-flow, not wave-helper-lane, because they
fire on the divergent-branch surface rather than the helper-lane /
wave-op surface. The two categories share Phase 4 infrastructure
(ADR 0013) but target different authoring mistakes.

## Run the wave-helper-lane rules

The two shipped rules fire by default at `warn` severity. To see them
on a shader with helper-lane interactions:

```sh
./build/cli/hlsl-clippy lint --format=github-annotations shaders/PS_*.hlsl
```

If your shader genuinely wants to exclude helpers from a wave
reduction (debug visualisation, coverage-aware paths), suppress
per-line:

```hlsl
float waveAvg = WaveActiveSum(uv.x) // hlsl-clippy: allow(wave-reduction-pixel-without-helper-attribute)
              / WaveActiveCountBits(true);
```

The two rules will not save 0.3 ms across a frame. They will, when
they fire, save you the kind of edge-case sparkle bug that takes
three days of artist time to root-cause and ships as a "TAA issue"
in the patch notes — a worthwhile trade for a category with two
rules in it.

---

`hlsl-clippy` is open source. Rules, issues, and discussion live at
[github.com/NelCit/hlsl-clippy](https://github.com/NelCit/hlsl-clippy).
If you have encountered a wave-intrinsic or helper-lane pattern that
should be a lint rule, open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
