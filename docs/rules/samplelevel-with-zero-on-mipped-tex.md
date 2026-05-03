---
id: samplelevel-with-zero-on-mipped-tex
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# samplelevel-with-zero-on-mipped-tex

> **Pre-v0 status** — this rule is documented ahead of its implementation. The detection logic ships in Phase 3. Behaviour described here is the design target, not yet enforced by the tool.

## What it detects

Calls to `SampleLevel(sampler, uv, 0)` — or `SampleLevel(sampler, uv, 0.0)` — where the third argument (the LOD parameter) is the literal zero, and where reflection data shows that the bound resource was declared with mip levels (a full mip chain or a partial chain with more than one level). The rule fires when a `Texture2D`, `TextureCube`, `Texture2DArray`, or similar mipped resource type is paired with an explicit mip-0 lock that is not guarded by a compile-time constant or a `[mips(1)]` resource annotation. It does not fire on resources explicitly declared as single-mip (`Texture2D<float4> T : register(t0); // mips 1`), on `Buffer<>` or `RWTexture2D<>` objects, or when the lod argument is a non-zero expression.

## Why it matters on a GPU

Mip chains exist for two reasons: reducing aliasing in minification, and enabling the texture cache to fetch smaller footprints from VRAM when the on-screen footprint is small. When a mipped resource is sampled exclusively at mip 0, every fetch hits the highest-resolution level regardless of the rasterised pixel footprint. This defeats both goals: cache lines are wasted on high-resolution texels that are averaged away by the hardware's aniso filter, and the lower mip levels occupy VRAM bandwidth during allocation and streaming for no return. A 2048x2048 RGBA8 texture carries 4 MB at mip 0 alone; pinning all samples to mip 0 means the additional 1.3 MB of mip chain is uploaded and retained in VRAM but never touched.

From the TMU perspective, `SampleLevel` bypasses implicit-derivative calculation and forces the hardware to use the specified LOD directly. This is intentional and correct when the caller knows the right mip level — for example, when sampling a shadow map, a lookup table, or a compute-shader read where no quad context exists. However, a pixel shader that samples a normal map or albedo at `SampleLevel(s, uv, 0)` throughout its execution disables trilinear blending and may produce shimmering on distant surfaces because the hardware will not fall back to a coarser mip even when the on-screen footprint would warrant it. The visual result can look correct in a close-up test case and only manifest as quality regression at lower resolutions or on temporally-reprojected frames.

The rule is a `suggestion` rather than `machine-applicable` because the correct replacement depends on caller context: in a pixel shader entry point, `Sample(sampler, uv)` is usually the right fix (restoring implicit derivatives and trilinear LOD selection); in compute or any non-quad-uniform context, a non-zero `SampleLevel` argument derived from `CalculateLevelOfDetail` or from a push-constant may be the right answer. The diagnostic indicates which resource the rule triggered on, the line number, and the resource's declared mip count as reported by Slang reflection.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/textures.hlsl, line 17
// HIT(samplelevel-with-zero-on-mipped): explicit mip 0 on a mipped texture.
Texture2D Normal : register(t1);  // mipped
SamplerState Bilinear : register(s0);

float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 n = Normal.SampleLevel(Bilinear, uv, 0).xyz * 2.0 - 1.0;
    // ...
}
```

### Good

```hlsl
// Pixel shader: restore implicit LOD selection so trilinear blending works.
float4 entry_main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 n = Normal.Sample(Bilinear, uv).xyz * 2.0 - 1.0;
    // ...
}

// Compute shader where no quad context exists: use a calculated or known LOD.
[numthreads(8, 8, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID) {
    float2 uv = (float2(dtid.xy) + 0.5) * InvScreenSize;
    float lod = Normal.CalculateLevelOfDetail(Bilinear, uv);
    float3 n = Normal.SampleLevel(Bilinear, uv, lod).xyz * 2.0 - 1.0;
}
```

## Options

none

## Fix availability

**suggestion** — The rule can propose replacing `SampleLevel(s, uv, 0)` with `Sample(s, uv)` in pixel shader entry points. Because the correct substitute depends on the call site's shader stage and the intent of the original author, the fix requires verification before application. Use `// shader-clippy: allow(samplelevel-with-zero-on-mipped-tex)` to suppress at a specific call site when locking mip 0 is intentional (for example, when sampling a single-mip render target masquerading as a full-mip resource).

## See also

- Related rule: [`samplegrad-with-constant-grads`](samplegrad-with-constant-grads.md) — zero gradients also force a specific LOD level
- Related rule: [`texture-as-buffer`](texture-as-buffer.md) — resources accessed exclusively at mip 0 in a linear pattern may be better declared as `Buffer<>`
- HLSL intrinsic reference: `Texture2D.SampleLevel`, `Texture2D.Sample`, `Texture2D.CalculateLevelOfDetail` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/samplelevel-with-zero-on-mipped-tex.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
