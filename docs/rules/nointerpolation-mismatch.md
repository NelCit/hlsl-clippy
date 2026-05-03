---
id: nointerpolation-mismatch
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# nointerpolation-mismatch

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A vertex-output / pixel-input attribute that the pixel shader uses in a flat-only context — read as an integer (`asint`, `(uint)`), used as an array index, used as a `switch` selector, used as a buffer/texture index, or compared with `==` against a per-primitive integer constant — but whose declaration on the vertex-output side does not carry the `nointerpolation` modifier. The rule cross-references the matched VS-output struct member (or geometry/mesh-shader output) with the PS-input usage and fires on the VS-side declaration, suggesting that `nointerpolation` be added so the rasteriser broadcasts the provoking-vertex value unchanged instead of computing a barycentric-weighted blend.

## Why it matters on a GPU

The rasteriser's default interpolation mode is barycentric: for each pixel sample, the hardware computes `attr_pixel = w0*attr_v0 + w1*attr_v1 + w2*attr_v2`, where `w0..w2` are the perspective-correct barycentric weights. On AMD RDNA 2 and RDNA 3, this is performed by the parameter cache and the per-pixel `v_interp_*` instructions, each of which consumes a VALU slot. On NVIDIA Turing and Ada Lovelace, the SM's attribute interpolator unit issues an `IPA` instruction per attribute component per pixel — Turing's whitepaper documents `IPA` as a half-rate operation against general FP32 throughput, and Ada inherits the same scheduling. For a 4-component attribute that is then immediately cast to `int` and used as an index, every one of those interpolator cycles is wasted: the fractional output is discarded the instant the cast happens.

There is also a precision risk that compounds the throughput cost. Barycentric interpolation is computed in single-precision floating point, with the weights themselves derived from rasterised pixel-centre positions. For a value that is logically an integer (a material ID, an instance ID, a bone index), interpolating it across three vertices that all carry the same integer value will, in exact arithmetic, return that integer back — but in float32, the multiplications and adds introduce ULP-level error. A material ID of `127` interpolated across three vertices that all hold `127.0f` may emerge from the interpolator as `126.9999847f`, which truncates to `126` after `(int)` conversion, indexing into the wrong material slot. This is a classic source of one-pixel-wide flickering edges between primitives on shared-vertex geometry.

`nointerpolation` instructs the rasteriser to skip plane-equation evaluation for the affected slot entirely: the value at the provoking vertex (vertex 0 by default in D3D, configurable in Vulkan) is broadcast to every pixel of the primitive unchanged. On AMD RDNA, the parameter cache stores a single value per primitive instead of three vertex values plus barycentric weights, freeing both LDS footprint and the per-pixel `v_interp_*` issue slots. On NVIDIA Ada, `IPA.CONSTANT` is a single-cycle attribute fetch that bypasses the multiply-add path entirely. The combined effect is fewer interpolator cycles per pixel, smaller parameter-cache footprint, and exact integer fidelity across the primitive.

## Examples

### Bad

```hlsl
struct VsToPs {
    float4 position   : SV_Position;
    float4 worldPos   : TEXCOORD0;
    float  materialId : TEXCOORD1;  // missing 'nointerpolation' — rasteriser blends it
};

float4 PSMain(VsToPs input) : SV_Target {
    // Forced barycentric interpolation on a logical integer:
    // - wastes IPA cycles on every pixel
    // - risks ULP drift turning material 127 into 126 at primitive edges
    uint id = (uint) input.materialId;
    return MaterialTable[id].albedo;
}
```

### Good

```hlsl
struct VsToPs {
    float4 position   : SV_Position;
    float4 worldPos   : TEXCOORD0;
    nointerpolation float materialId : TEXCOORD1;  // broadcast from provoking vertex
};

float4 PSMain(VsToPs input) : SV_Target {
    // Single-cycle constant fetch; exact integer round-trip.
    uint id = (uint) input.materialId;
    return MaterialTable[id].albedo;
}
```

## Options

none

## Fix availability

**suggestion** — Adding `nointerpolation` to a vertex-output member changes the value seen by every pixel: smooth-interpolated `127.5` becomes a flat `127.0` (or whatever the provoking-vertex value is). When the consuming code path treats the value as a smooth quantity in some branches and a flat one in others, the change is not safe to apply automatically. The diagnostic shows the proposed edit alongside the PS-side usage that flagged it, so the author can verify intent before applying the fix.

## See also

- Related rule: [`excess-interpolators`](excess-interpolators.md) — total interpolator slot pressure
- Related rule: [`unused-vs-output`](unused-vs-output.md) — vertex-output members that no consuming PS reads at all
- HLSL reference: `nointerpolation`, `noperspective`, `centroid`, `sample` — interpolation modifiers in the DirectX HLSL semantics documentation
- AMD RDNA ISA reference: `v_interp_p1_f32`, `v_interp_p2_f32`, `v_interp_mov_f32`
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/nointerpolation-mismatch.md)

*© 2026 NelCit, CC-BY-4.0.*
