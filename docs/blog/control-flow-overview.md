---
title: "Divergent control flow is the silent killer of your shader"
date: 2026-05-01
author: NelCit
category: control-flow
tags: [hlsl, shaders, performance, control-flow, simt, divergence, helper-lanes]
license: CC-BY-4.0
---

A pixel shader walks into a bar. It samples a texture, alpha-tests the
result, and `discard`s the lanes that fail. After the discard it computes
a `WaveActiveSum` of the surviving lanes' luminance and writes the average
back. The shader compiles clean, validates clean, runs without a TDR.
Output is wrong by an amount that depends on screen content, alpha-test
hit rate, and how the driver packed the wave that frame — which is to
say, wrong by an amount QA will not reproduce locally and that will get
chalked up to a "TAA bug" and shipped.

This post is about that class of bug, and the broader family of
control-flow patterns it belongs to. The unifying thread is that GPUs do
not execute scalar code on independent threads. They execute SIMT code on
wide vector hardware where the wave is the unit of execution, divergence
has hardware-defined consequences, and helper lanes are not a polite
fiction the framebuffer cleans up afterwards — they are real lanes that
consume real cycles and participate in real cross-lane operations.

The control-flow rule pack in `hlsl-clippy` exists to surface the patterns
that go wrong when shader authors think in scalar threads instead of waves.
Twenty-one rules, all rooted in mechanics that are documented in the DXIL
spec, the AMD RDNA ISA reference, and the NVIDIA PTX manual — but rarely
explained in one place from a graphics-engineer perspective. This post is
the one-place explanation; the per-rule pages have the surgical detail and
the autofix specifics.

## The wave / warp / wavefront model

A modern GPU does not execute one thread of HLSL at a time. It executes
**waves** — also called **warps** on NVIDIA, **wavefronts** on AMD, **EU
threads** on Intel — of 8 to 64 lanes that share a single program counter.
On AMD RDNA 2 and RDNA 3, a wave is 32 or 64 lanes (configurable via the
SM 6.6 `[WaveSize(N)]` attribute, or driver-default 32 for compute and 64
for some pixel paths on RDNA 2). On NVIDIA Turing, Ampere, Ada, and
Blackwell, a warp is 32 lanes. On Intel Xe-HPG, the SIMD width is variable
across SIMD8, SIMD16, and SIMD32 depending on register pressure and shader
shape, and the compiler picks at link time.

When all lanes of a wave agree on a control-flow decision, execution is
straightforward: the program counter advances, every lane runs the same
instruction, life is good. When lanes disagree — a divergent branch —
the hardware serialises the arms. On AMD RDNA the `EXEC` mask register
gates which lanes write back results; on NVIDIA the equivalent is the
warp-wide active mask managed by the convergence barriers introduced in
Volta and refined in Ampere; on Intel Xe-HPG the `EMASK` register tracks
active channels per dispatch. In all three cases, lanes that took the
false arm sit idle while the true arm runs, then swap roles. The hardware
costs both arms in clock cycles but only writes back results from the
matching half of the wave. This is **predication**, and on a divergent
branch it is the cost floor — there is no way to skip the inactive arm
without breaking the SIMT contract.

The corollary is that a **uniform** branch is genuinely free in a way that
a divergent one is not. If every lane in the wave evaluates the predicate
to true, the inactive arm is skipped wholesale: zero cycles for the false
arm, zero VGPRs allocated for its temporaries, zero TMU traffic from its
texture samples. The hardware can do this because there is no result to
predicate — every lane agreed. The compiler cannot always tell that a
branch is uniform without help, which is where attributes come in.

## Divergent branches and the [branch] / [flatten] heuristics

HLSL exposes four loop / branch attributes that tell the compiler how to
lower an `if`, `switch`, `for`, or `while`: `[branch]`, `[flatten]`,
`[loop]`, and `[unroll]`. Each one fixes a specific lowering choice that
the compiler would otherwise make heuristically.

`[branch]` says: emit a real conditional jump and let the wave skip the
inactive arm when the predicate is uniform. `[flatten]` says: predicate
both arms unconditionally — every lane runs every instruction in both
arms, and the write mask sorts out which results to keep. The two are
inverses of each other on the cost-model axis. On a uniform predicate,
`[branch]` skips the inactive arm; `[flatten]` does not, and burns 100% of
the inactive arm's cycles on every wave. On a divergent predicate, both
attributes run both arms — the difference shrinks to a code-size and
register-pressure preference rather than a cycle delta.

The bug is asymmetric. Defaulting to `[flatten]` on a uniform predicate
costs you the entire false arm on every dispatch — a 30-instruction
shading variant runs on every wave instead of being skipped. Defaulting
to `[branch]` on a divergent predicate costs you a marginal scheduling
penalty on the convergence point but does not double work. The
`branch-on-uniform-missing-attribute` rule
([rules/branch-on-uniform-missing-attribute](/rules/branch-on-uniform-missing-attribute))
catches the case where a `cbuffer` mode flag drives an `if` without
`[branch]` — exactly the pattern where a tone mapper or quality-tier
switch silently doubles its ALU cost because the compiler defaulted to
predication. The complementary `flatten-on-uniform-branch` rule
([rules/flatten-on-uniform-branch](/rules/flatten-on-uniform-branch))
catches the inverse mistake: a defensive `[flatten]` copy-pasted from a
divergent context onto a uniform branch, where the cost model has flipped
under the author's feet.

For loops, the same story plays out with `[unroll]` versus `[loop]`. A
constant-bounded small loop without `[unroll]` pays a per-iteration counter
update and a backward branch — fine on a 64-lane wave because the branch
is wave-uniform, but the back-edge prevents the compiler's scheduler from
interleaving instructions across iterations to hide texture latency. The
`small-loop-no-unroll` rule
([rules/small-loop-no-unroll](/rules/small-loop-no-unroll)) flags the
4-tap blur and 16-tap convolution patterns that miss this annotation.

## Helper lanes: the invisible passengers in your pixel shader

Pixel shaders are special. They do not execute in waves of arbitrary
threads — they execute in waves composed of 2x2 quads, four pixels arranged
in a square that share screen-space derivatives. The quad is the
fundamental scheduling unit because `ddx`, `ddy`, and the implicit-LOD
sampler path all read coordinate values from the four lanes of the quad
and form differences. If any quad lane is missing, the derivative is
garbage.

The hardware preserves the quad even when the rasteriser only covers some
of its pixels. The non-covered pixels become **helper lanes**: they
execute every instruction the active pixels execute, hold valid register
state, participate in derivatives, and are then dropped at framebuffer
write. They are not free — they consume VGPRs, they issue TMU requests,
they pay every cycle of pixel-shader cost. Their only purpose is to keep
the quad's derivatives well-defined.

When you `discard` a pixel, it does not exit the shader. It becomes a
helper lane for the rest of the function's lifetime. Every subsequent
texture sample, every subsequent ALU op, every subsequent UAV access runs
on the discarded lane just as it does on the survivors — only the
framebuffer write is suppressed. On heavily alpha-tested geometry (foliage,
wire mesh, particle systems) where 50% or more of pixels discard, this
means the shader effectively runs at 100% cost for 50% of useful output.
The `discard-then-work` rule
([rules/discard-then-work](/rules/discard-then-work)) catches significant
work — multi-tap loops, expensive samples — placed after a `discard`
guard that could have run before it. Hoisting the work is a free win when
the discarded pixels do not need a different code path.

The conditional `discard` has a second cost outside helper-lane semantics:
it disables **early-Z**. Modern depth/stencil hardware can reject hundreds
of fragments per clock before any shader work runs, but only if the
depth value is fixed at rasteriser output. A shader that may `discard`
makes the depth value dependent on shading work, so the driver demotes the
test to **late-Z** — every covered fragment gets shaded in full, then
tested. On an opaque deferred prepass at 1080p with high overdraw, this
flips a 5-20x cost ratio against you for every draw using the shader.
The `early-z-disabled-by-conditional-discard` rule
([rules/early-z-disabled-by-conditional-discard](/rules/early-z-disabled-by-conditional-discard))
catches the case where adding `[earlydepthstencil]` to the entry point
would force early-Z back on. The annotation is safe whenever the
`discard` only affects colour, which is the common case for alpha-tested
materials.

The helper-lane / wave-intrinsic interaction —
`WaveActiveSum` over discarded survivors picking up stale helper-lane
values — is its own deep mechanism, covered in the
[wave-helper-lane category overview](/blog/wave-helper-lane-overview).
Cross-link: the `wave-intrinsic-helper-lane-hazard` rule
([rules/wave-intrinsic-helper-lane-hazard](/rules/wave-intrinsic-helper-lane-hazard))
is where this post's opening anecdote lives.

## Derivatives in non-uniform control flow

`Texture.Sample(s, uv)` looks like a single intrinsic, but at the hardware
level it reads `uv` from all four lanes of the quad, computes
`ddx(uv) = uv[1] - uv[0]` and `ddy(uv) = uv[2] - uv[0]`, picks a mip
level from the gradient magnitudes, and only then issues the actual fetch.
The first three steps require all four quad lanes to hold valid `uv`
values — values they would have computed if they had executed the same
code path as the active lane.

When a `Sample` call sits inside a divergent `if`, this contract breaks.
The masked-off lanes did not execute the code in the branch; their
register state for `uv` is whatever was last assigned outside the branch,
which has no defined relationship to what `uv` should be inside it. The
hardware does not detect this and does not raise an exception. It computes
derivatives from those stale values, picks a wrong mip level, and returns
data sampled from the wrong frequency band of the texture. The result is
mip-thrash speckle, ghosting, or worse — and it is undefined behaviour
under the DXIL spec, so different drivers and compiler versions may
produce different wrong outputs.

The `derivative-in-divergent-cf` rule
([rules/derivative-in-divergent-cf](/rules/derivative-in-divergent-cf))
fires on `ddx`, `ddy`, `Sample`, and `SampleBias` calls inside non-uniform
branches. The fixes are mechanical: hoist the sample before the branch
(when the texture and `uv` are already defined), or pre-compute `ddx_uv`
and `ddy_uv` in uniform CF and switch to `SampleGrad`, which does not
depend on cross-lane derivatives. The `sample-in-loop-implicit-grad` rule
([rules/sample-in-loop-implicit-grad](/rules/sample-in-loop-implicit-grad))
catches the loop variant of the same hazard, where per-pixel-varying loop
bounds make the quad lanes leave the loop at different iterations.

## Barriers in divergent control flow

The compute-shader equivalent of the helper-lane footgun is barrier
placement. `GroupMemoryBarrierWithGroupSync()` tells the hardware to stall
every thread in the group until all of them have reached this instruction.
The hardware implements this with a check-in counter; only when the count
matches the group size does the barrier release. If some threads never
arrive — because they took a divergent branch and the barrier sat inside
its true arm — the counter never saturates and the entire compute unit
hangs. On AMD RDNA the wavefront cannot be retired and the CU stalls; on
NVIDIA the warp deadlocks against its own convergence barrier; the
runtime cannot detect this at API level, and it surfaces as a TDR or
device-removed error in production.

The non-syncing variants (`GroupMemoryBarrier` without `WithGroupSync`)
are no safer. They flush caches for memory ordering, and a non-uniform
flush gives non-uniform observability — threads that skipped the barrier
may observe stale LDS data written by threads that took it, which is a
data race on groupshared memory and undefined behaviour by the D3D12 spec.

The `barrier-in-divergent-cf` rule
([rules/barrier-in-divergent-cf](/rules/barrier-in-divergent-cf)) fires on
any `GroupMemoryBarrier*` or `DeviceMemoryBarrier*` reachable only on a
non-uniform path. The most common offender is the early-exit guard:

```hlsl
[numthreads(64, 1, 1)]
void cs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= count) return;          // half the wave exits here
    LDS[dtid.x] = data[dtid.x];
    GroupMemoryBarrierWithGroupSync();    // hang: 32 of 64 threads will never arrive
    ...
}
```

The fix is to keep all threads alive through the barrier, then diverge
afterwards — or equivalently, compute the contributing data unconditionally
(zeroing the out-of-range writes) and let every thread reach the barrier.

## Loop-invariant patterns the compiler will not always hoist

The last family of control-flow rules is about work that the compiler is
allowed to hoist out of a loop or out of branch arms but does not always
choose to. The autofix in this case is human work, not because the
transform is unsafe but because the compiler's loop-invariant code motion
(LICM) pass is conservative about side effects.

A texture sample inside a loop with a loop-invariant UV — the most
common form is a 16-tap blur where one of the taps is the centre pixel
that does not move with the loop counter — issues 16 identical TMU
requests where one would do. The compiler does not hoist it because it
cannot prove the texture contents have not changed between iterations
(GPU resource aliasing analysis is conservative), and because the VGPR
pressure to hold the result live across iterations weighs against the
saving in its cost model. The
`loop-invariant-sample` rule
([rules/loop-invariant-sample](/rules/loop-invariant-sample)) flags the
pattern; the fix is a one-line hoist that costs 4 bytes of VGPR per lane
per `float4` and recovers 15 of 16 TMU requests.

The branch-arm equivalent is an expression that appears identically in
both `then` and `else`. Under predication — the default lowering for
short-arm branches — both arms execute every lane, so a duplicated
`pow(base.a, 5.0)` or `dot(rgb, float3(0.2126, 0.7152, 0.0722))` runs
twice on every pixel regardless of which arm wins. The
`redundant-computation-in-branch` rule
([rules/redundant-computation-in-branch](/rules/redundant-computation-in-branch))
hoists the expression to before the `if` and replaces both occurrences
with the new temporary. This is the rare control-flow rule that ships as
machine-applicable: when the expression is pure (no implicit-derivative
samples, no UAV writes), the rewrite is provably semantics-preserving.

## What DXC and Slang catch, and what they do not

DXC's optimisation pipeline is good at the local cases. A constant-bounded
loop with a small body, a uniform branch with a `[branch]` already
attached, a pure expression duplicated trivially across two arms — all of
these survive a release-mode build with the right thing happening. The
cases the linter exists for are the cases the compiler cannot prove.

DXC will not insert `[branch]` on your behalf when the predicate is
uniform-by-calling-convention (a `cbuffer uint Mode` that the engine
guarantees is set identically per-draw). The uniformity is a property of
the runtime, not the type system. The linter's uniformity oracle reasons
about the same property — `cbuffer` fields, `nointerpolation` interpolants
not driven by per-pixel data, `WaveReadLaneFirst` results — and emits the
suggestion the compiler cannot make safely. DXC will also not catch
`[flatten]` on a uniform predicate, because syntactically the attribute is
valid; the rule lives in the cost-model layer above.

DXC will not warn on `discard` inside a non-uniform branch followed by a
`Sample` — the call is legal, and the spec says the result is undefined,
but no diagnostic surfaces. DXC will not warn on `WaveActiveSum` after a
discard, because the wave intrinsic is well-typed and the helper-lane
participation rules are not part of the type system. DXC will not warn on
`GroupMemoryBarrierWithGroupSync` inside a divergent branch, because
correctness depends on data values it cannot prove.

The static-analysis layer that closes this gap is what the
control-flow rule pack does. It builds a control-flow graph over the
tree-sitter AST, runs a uniformity oracle over the CFG, and reasons about
helper-lane state and barrier reachability path-sensitively. The
infrastructure is documented in
[ADR 0013](https://github.com/NelCit/hlsl-clippy/blob/main/docs/decisions/0013-phase-4-control-flow-infrastructure.md);
the rule pages explain the surface.

## Run the control-flow rules on your shaders

The full list lives at
[/rules/?category=control-flow](/rules/?category=control-flow). Twenty-one
rules, every one with a documented GPU mechanism, examples, and a
suppression syntax for the cases where you have profiled and disagreed
with the diagnostic. Drop the linter into your build, narrow the run to
the control-flow category for the first pass, and address the
`error`-severity hits first — `wave-intrinsic-non-uniform`,
`barrier-in-divergent-cf`, and `derivative-in-divergent-cf` are all
flagged at error level because the underlying behaviour is UB, not
just slow. The `warn`-severity rules are performance regressions you can
schedule. Both kinds get a per-rule blog post explaining the mechanism
once those land alongside their rule's release.

The pixel shader from the opening anecdote — `discard` followed by
`WaveActiveSum` — fails the `wave-intrinsic-helper-lane-hazard` and
`discard-then-work` rules together. The fix is to gate the wave reduction
with `IsHelperLane()` (SM 6.6+) so helpers do not contribute their stale
colour values, or to hoist the work before the discard so helpers were
never in the wave reduction in the first place. Either fix is a few
lines. The bug ate three days of artist time before someone profiled it
on hardware. That is the gap this rule pack is designed to close.

---

`hlsl-clippy` is open source. Rules, issues, and discussion live at
[github.com/NelCit/hlsl-clippy](https://github.com/NelCit/hlsl-clippy).
If you have encountered a control-flow pattern that should be a lint
rule, open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
