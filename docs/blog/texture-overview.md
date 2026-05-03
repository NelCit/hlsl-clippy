---
title: "Texture sampling is doing more work than your shader admits"
date: 2026-05-01
author: NelCit
category: texture
tags: [hlsl, shaders, performance, texture, sampler, anisotropy, format]
license: CC-BY-4.0
---

A `Sample` call looks like a function call. It is not. It is a request to a
fixed-function block of silicon that lives outside the shader core,
schedules its own memory transactions, performs its own format decoding,
runs its own filter math, and hands the result back as a `float4` whose
bit-exact value depends on whether the calling lane is alive, what the
neighbouring three lanes are doing, what format the underlying resource
actually is, and which channel order the storage uses. Most of that detail
is invisible at the HLSL level. The compiler will not warn you when you
get it wrong. The driver will not warn you. The hardware will produce a
plausible-looking image and you will ship the bug.

Consider the canonical opener:

```hlsl
for (int i = 0; i < taps; ++i)
{
    float2 uv = base_uv + offsets[i];
    if (mask[i])
        accum += atlas.Sample(s, uv);
}
```

Every line here is normal HLSL. Every line is also a footgun. The `Sample`
call sits inside both a loop and a divergent `if`. The loop has a per-pixel
trip count if `taps` is non-uniform. The branch condition is not provably
uniform across the 2x2 quad. And `Sample` derives its mip level from
implicit screen-space derivatives — derivatives that are computed across
all four lanes of the quad, including lanes that are masked off, including
lanes that exited the loop early. The D3D12 spec is explicit: this is
undefined behaviour. The output is whatever the hardware computes when it
differences a live coordinate against a stale coordinate sitting in a
masked lane's register, and the answer changes between driver versions.

This post is about the silicon underneath the `Sample` call, why it is
fixed-function on every IHV that ships, and the five most common ways HLSL
asks it to do work it should not.

## What the sampler actually is

The sampler unit on every modern GPU is a separate, fixed-function block.
It is not part of the shader core. The shader issues a sample request, the
sampler unit goes off and does the work, and the shader gets the result
back in a register some unspecified number of cycles later.

On AMD RDNA 2 and RDNA 3, the unit is called the **TA / TC pair** — the
Texture Address unit (TA) computes the addresses, applies the filter
footprint, handles cube-face selection, and the Texture Cache (TC) backs
it with a small L0 sitting in front of L1. On NVIDIA Turing and Ada
Lovelace, the **TMU** (Texture Mapping Unit) plays both roles. Each
NVIDIA SM has 4 TMUs that operate at one filtered sample per clock per
TMU, paired with a per-TMU L1 cache. On Intel Xe-HPG, the equivalent unit
is the **sampler subsystem** of the data port, which handles
address-and-format conversion as a discrete pipeline stage upstream of
the EU.

Across all three IHVs, four operations happen inside this fixed-function
block on every sample:

1. **Address computation.** UV * size + base, mip selection from
   derivatives, cube-face selection, wrap/clamp/border.
2. **Footprint and tap generation.** Bilinear is 4 taps. Trilinear is 8.
   Anisotropic is up to 2 * `MaxAnisotropy` taps along the major axis.
3. **Format decode.** UNORM / SNORM scaling, sRGB-to-linear conversion,
   BC block decompression, channel reorder for BGRA storage.
4. **Filter math.** Weighted sum across taps, with sub-bit precision the
   shader cannot reproduce in 32-bit float.

None of this runs on the VALU. None of it shows up in your shader's
instruction count. But every step has a contract with the hardware that,
when you violate it from HLSL, produces wrong pixels and never an error.

## Sample-in-loop and the helper-lane derivative trap

The opening example is undefined behaviour. Pixel shaders execute as
2x2 quads — the smallest unit at which the rasterizer guarantees the
four lanes are co-resident on the same SIMD. Implicit-derivative
sampling computes mip selection by differencing the texture coordinate
against the three other lanes in the quad. On RDNA 2/3, this is
implemented by the `image_sample` instruction reading the S/T operands
across all four quad lanes through the cross-lane permute network. On
Turing and Ada, the TMU consumes coordinates from all four quad lanes
in parallel and forms `ddx`/`ddy` in dedicated derivative-computation
hardware before issuing the actual fetch.

When the four lanes do not agree on whether to execute the `Sample` —
because the call sits inside a divergent `if`, because a loop trip count
varies per pixel, because a quad lane took an early `discard` — the
derivative read pulls coordinate values from inactive lanes. The
hardware does not abort. It returns whatever bit pattern is sitting in
the masked lane's register: stale data from a previous instruction,
zero-initialised garbage, or, on some drivers, a deliberately-poisoned
NaN. Mip selection is then wrong by an unbounded factor. The visual
regression is shimmer, sparkle, or full-frame stippling that
mysteriously changes when you upgrade DXC.

The fix is one of two paths. Use `SampleLevel(s, uv, lod)` with an
explicit LOD — this disconnects the sample from quad-coupled
derivatives entirely and is the right call for UI, post, and most
compute-style passes. Or use `SampleGrad(s, uv, ddx_uv, ddy_uv)` with
gradients computed in *uniform* control flow before the divergent
region; this preserves trilinear / anisotropic filtering with the
gradients you control. The
[`sample-in-loop-implicit-grad`](/rules/sample-in-loop-implicit-grad)
rule catches the loop / divergent-branch / non-inlined-function-with-
mixed-callsites cases and the
[`samplegrad-with-constant-grads`](/rules/samplegrad-with-constant-grads)
rule catches the inverse footgun where a `SampleGrad` is called with
gradients that are loop-invariant constants and could have been
`SampleLevel` instead.

## BGRA versus RGBA: the silent channel swap

```hlsl
float4 ui = ui_atlas.Sample(s, uv);
output = ui;  // looks fine, ships fine on Windows, wrong on every other surface
```

`DXGI_FORMAT_B8G8R8A8_UNORM` is the historical swap-chain format for
D3D presentation. Many older paths — IMGUI overlays, debug HUDs,
swap-chain compositing — still use BGRA8 throughout. The hardware
texture sampler on RDNA 2/3, Turing/Ada, and Xe-HPG reads BGRA8 storage
and presents it to the shader as a `float4` whose `.x` lane carries the
*blue* channel and `.z` lane carries the *red* channel. There is no
hardware swizzle on the load path on D3D12: the format-to-shader-view
mapping is exactly the storage layout. Vulkan exposes a per-view
`componentMapping` swizzle that can compensate at descriptor creation;
D3D12 does not.

The bug surfaces when the shader treats the loaded `float4` as if `.r`
is conceptually "red", uses it in subsequent colour math, and writes to
an RGBA8 render target. Red and blue swap silently. Sky becomes orange,
skin becomes blue, and the cause is buried in the format-vs-convention
mismatch between swap-chain BGRA and asset RGBA. UI code that mixes
the two is the canonical source.

The
[`bgra-rgba-swizzle-mismatch`](/rules/bgra-rgba-swizzle-mismatch) rule
cross-references the sample call site's swizzle (or absence thereof)
against the resource format reported by Slang reflection. It fires when
a BGRA-format resource is read without a compensating `.bgra` swizzle.
This is exactly the kind of bug a reflection-aware linter catches that
no AST-only tool can.

## UNORM / SNORM round-trips: the duplicate divide

```hlsl
float4 c = tex.Sample(s, uv);
float r = c.r * (1.0 / 255.0);  // why?
```

A UNORM texture format (`R8_UNORM`, `R8G8B8A8_UNORM`, `R16_UNORM`)
carries an explicit hardware contract: every sampling operation returns
a 32-bit float already normalised to `[0, 1]`. The conversion happens
in fixed-function silicon — on RDNA 2/3 it is folded into the TMU's
output formatter, on Turing and Ada it is the texture-pipeline's
address-mode-and-format converter that runs in parallel with the LERP
unit, on Xe-HPG it is the sampler's data-port conversion stage. The
cost is zero ALU because no ALU runs: the conversion is a side-effect
of the load that the texture unit performs whether the shader asks
for it or not. SNORM works the same way except the format converter
sign-extends and remaps to `[-1, 1]`.

When the shader writes `tex.Sample(...).r * (1.0 / 255.0)`, the texture
unit has already produced a float in `[0, 1]`. The explicit scale is
*duplicating* a conversion that already ran on the texture path. That
extra `v_mul_f32` (RDNA), `FMUL` (Turing/Ada), or EU multiply (Xe-HPG)
is one VALU per component per sampled pixel, on a code path that is
hot by definition.

It also changes the numbers. The true UNORM-to-float identity is
`v / (2^N - 1)` for an N-bit channel, which `* (1.0 / 255.0)` reproduces
for `R8` only by coincidence of the literal. For `R16_UNORM` the same
anti-pattern with `* (1.0 / 65535.0)` divides the already-normalised
result a *second* time and produces values around `1 / 65535` of the
correct magnitude — a silent magnitude bug that is easy to miss until
the texture goes black on screen. The
[`redundant-unorm-snorm-conversion`](/rules/redundant-unorm-snorm-conversion)
rule pattern-matches the literal scaling factors that uniquely
identify UNORM (`1/255`, `1/65535`) and SNORM (`2/255 − 1`, `1/127`)
decoding and flags them when applied to the result of a `Sample` /
`Load` / `Gather`. The same mechanism applies to manual sRGB
conversion: when an `*_SRGB` format already runs the IEC 61966-2-1
piecewise transfer in hardware, an explicit `pow(c, 2.2)` on top of
the result double-converts and darkens the mid-tones — the
[`manual-srgb-conversion`](/rules/manual-srgb-conversion) rule
catches that.

## Gather: pick the channel at the instruction, not the swizzle

```hlsl
float shadow = depth_tex.Gather(s, uv).r;  // works, but ask for what you want
```

`Gather` is a TMU operation that fetches the four texels in the 2x2
bilinear footprint surrounding a UV and returns one scalar channel
from each texel packed into a `float4`. On every current architecture
— RDNA / RDNA 2 / RDNA 3, Turing / Ada, Xe-HPG — a single `Gather`
instruction completes in one TMU issue cycle regardless of which
channel it returns. The hardware has dedicated `GatherRed`,
`GatherGreen`, `GatherBlue`, `GatherAlpha` variants that select the
channel at the instruction encoding, not in a post-processing step.

When a shader writes `texture.Gather(s, uv).r`, the compiler must emit
a `Gather` and then extract `.r`. Most DXC versions targeting DXIL SM6
lower this to the correct `image_gather4_r` (RDNA) or `GATHER4_PO`
with the channel-select field, but the lowering depends on
optimisation level and DXC version. Using the explicit `GatherRed(s, uv)`
form guarantees the correct single-channel instruction without
depending on the compiler to recognise the narrowing pattern, and it
expresses intent unambiguously. The
[`gather-channel-narrowing`](/rules/gather-channel-narrowing) rule
flags `Gather(...).r` / `.g` / `.b` / `.a` patterns where the other
three components are provably dead and offers a machine-applicable
rewrite.

A related trap: the DXIL `Gather` and `GatherCmp` operations encode an
explicit `channel` byte. When the channel byte does not match the
swizzle the HLSL programmer writes, you get a silently wrong result —
the four texels packed into `.x .y .z .w` of the gather result follow
a counter-clockwise convention starting from the lower-left texel of
the 2x2 footprint, *not* the order most people expect. Doing manual
PCF over `Gather` results requires the texel-corner ordering to be
correct; doing it via `SampleCmp` with the hardware comparison
sampler avoids the question entirely. See
[`comparison-sampler-without-comparison-op`](/rules/comparison-sampler-without-comparison-op)
for the related case where a `SamplerComparisonState` is bound but
the shader is calling regular `Sample` (which returns the raw depth
without comparison and the hardware PCF path is wasted) and
[`gather-cmp-vs-manual-pcf`](/rules/gather-cmp-vs-manual-pcf) for
the manual-PCF-over-Gather case.

## Anisotropy: how many taps you actually pay for

The `MaxAnisotropy` field of a sampler descriptor is consumed by the
hardware anisotropic filtering path *only* when the `Filter` selector
requests anisotropic filtering. RDNA 2/3 routes anisotropic samples
through the TMU's anisotropic footprint estimator, which computes the
elongation of the sample footprint in texture space and issues up to
`MaxAnisotropy` taps along the major axis. NVIDIA Turing/Ada and
Intel Xe-HPG implement equivalent anisotropic taps. The cost is
linear in the anisotropy ratio: 16x AF on a 45-degree surface costs
roughly 16 trilinear taps (with some optimisation when the elongation
is less than the cap), versus a single 4-tap bilinear or 8-tap
trilinear sample.

Two failure modes show up here. First, sampler descriptors that set
`MaxAnisotropy = 16` while the `Filter` selector is `LINEAR` — the
field is honestly ignored by the hardware, the texture is sampled
with linear filtering, and the descriptor lies about its filtering
quality. Reviewers and tooling that scan for "16x AF" status are
misled. The
[`anisotropy-without-anisotropic-filter`](/rules/anisotropy-without-anisotropic-filter)
rule catches this — it surfaces the inconsistency and asks the author
to either commit to AF (and pay the runtime cost) or set
`MaxAnisotropy = 1` and tell the truth.

Second, the inverse: a sampler set to anisotropic filtering used in a
context where the surface is screen-aligned and the elongation is
always 1.0. The hardware still pays the footprint-estimation cost
even when the result is one tap, and the descriptor occupies a slot
that could have held a cheaper sampler. There is no rule for this
yet — it requires runtime context the static linter does not have —
but it is on the candidate list for Phase 4 once the uniformity
oracle is in place.

A third surface worth flagging:
[`feedback-every-sample`](/rules/feedback-every-sample) — sampler
feedback is a streaming-aware extension that records which mip /
tile of a tiled resource was actually accessed. Every `WriteSamplerFeedback`
costs an extra TMU transaction; calling it on every sample (rather
than once per material per frame, or guarded by streaming-budget
conditions) hits the feedback resource on the hot path and is
almost always wrong.

## What this category does

Thirteen rules in the texture category, all of them rooted in a
documented hardware behaviour: the sampler unit is fixed-function
silicon with its own format decode, its own filter math, and its own
contract with the calling shader. The patterns above are the ones
that violate the contract while compiling clean and producing
plausible images.

Run `shader-clippy lint --format=github-annotations` against the texture
sampling in your hot pass. Read the doc page on the first warning.
Every rule explains the GPU mechanism in enough depth that you stop
writing the pattern in the first place — which is, eventually, the
point.

---

`shader-clippy` is open source. Rules, issues, and discussion live at
[github.com/NelCit/shader-clippy](https://github.com/NelCit/shader-clippy).
If you have encountered a shader pattern that should be a lint rule,
open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
