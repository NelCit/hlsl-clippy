---
id: excess-interpolators
category: bindings
severity: warn
applicability: none
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# excess-interpolators

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A vertex-to-pixel (or vertex-to-geometry, vertex-to-hull, mesh-to-pixel) interface struct whose total occupied `TEXCOORDn` slot count exceeds the configured hardware interpolator budget (default: 16 vec4 slots, matching the D3D12 SM6.x maximum of 32 four-component scalars across all input semantics counted per the runtime packing rules). The rule sums every member's slot footprint — `float4` consumes 1 slot, `float3`/`float2`/`float` round up to 1 slot each unless the compiler successfully packs them, and matrices and arrays multiply by their row/element count. The diagnostic fires on the struct declaration itself, naming the total slot count and listing the largest contributors.

## Why it matters on a GPU

Interpolated vertex attributes are not free: every `TEXCOORDn` slot occupies dedicated hardware storage between the rasteriser and the pixel shader. On AMD RDNA, RDNA 2, and RDNA 3, post-rasterisation attribute data is staged in the LDS (Local Data Share) of the CU running the pixel shader. The rasteriser writes per-vertex attribute values into LDS at primitive setup time; the pixel shader's parameter cache then reads barycentric-weighted samples from those LDS-resident values. LDS is a finite resource — 64 KB per workgroup processor on RDNA 2/3 — and is shared with groupshared memory, GS/HS export buffers, and the parameter cache itself. Interpolator pressure directly contests the same LDS pool that compute kernels and amplification shaders draw from, lowering pixel-shader wave occupancy on attribute-heavy workloads.

NVIDIA Turing and Ada Lovelace use a different staging path: per-vertex attributes are written into the SM's attribute interpolator unit (the "Pixel Parameter Cache" on Turing, expanded on Ada). The interpolator unit has a fixed capacity per SM — exceeding it forces the driver to split a single primitive's attribute payload across multiple cache fills, which serialises primitive setup. On Ada Lovelace, the documented per-SM attribute throughput is 16 vec4 attributes per cycle of setup; a struct with 24 vec4 slots takes 1.5x as long to admit into the pixel parameter cache, throttling primitive throughput at the rasteriser-to-PS handoff. Intel Xe-HPG uses URB (Unified Return Buffer) entries for the same purpose, with similar per-attribute slot pressure that competes with vertex-shader output buffering.

Beyond raw slot pressure, excess interpolators reduce primitive throughput in a second way: the parameter setup unit (which computes the per-attribute plane equation `A*x + B*y + C` for each interpolated value) is a fixed-throughput functional unit. Each additional interpolator slot adds another plane-equation evaluation per primitive at setup time. For triangle-heavy workloads (foliage, particle quads, GUI), the setup-unit ceiling is often the bottleneck before pixel shading even begins. Cutting an unused `float4 reserved` member from a vertex output struct can directly raise primitive throughput by the inverse of the slot count.

## Examples

### Bad

```hlsl
struct VsToPs {
    float4 position : SV_Position;
    float4 worldPos : TEXCOORD0;
    float4 normal   : TEXCOORD1;
    float4 tangent  : TEXCOORD2;
    float4 bitangent: TEXCOORD3;
    float4 uv01     : TEXCOORD4;
    float4 uv23     : TEXCOORD5;
    float4 color0   : TEXCOORD6;
    float4 color1   : TEXCOORD7;
    float4 lightVec : TEXCOORD8;
    float4 viewVec  : TEXCOORD9;
    float4 shadowCoord0 : TEXCOORD10;
    float4 shadowCoord1 : TEXCOORD11;
    float4 shadowCoord2 : TEXCOORD12;
    float4 shadowCoord3 : TEXCOORD13;
    float4 fogParams    : TEXCOORD14;
    float4 debug        : TEXCOORD15;
    float4 padding      : TEXCOORD16;  // 17 vec4 slots — over the default budget
};
```

### Good

```hlsl
// Pack pairs of vec2 attributes into vec4 slots; reconstruct tangent-frame
// bitangent from normal x tangent in the pixel shader; drop unused members.
struct VsToPs {
    float4 position    : SV_Position;
    float4 worldPos    : TEXCOORD0;  // .xyz world pos, .w fog factor
    float4 normalTan   : TEXCOORD1;  // .xyz normal, .w tangent.x
    float4 tangentRest : TEXCOORD2;  // .xy tangent.yz, .zw uv0
    float4 uv12        : TEXCOORD3;  // .xy uv1, .zw uv2
    float4 shadowCoord : TEXCOORD4;  // single-cascade shadow projection
};
```

## Options

- `slot-budget` (integer, default: `16`) — fire when the total `TEXCOORDn` slot count of a vertex-output struct exceeds this value. Set to `8` for tighter mobile budgets (Adreno 7xx, Mali-G7xx) or `32` to match the absolute D3D12 SM6.x ceiling.

  Example in `.hlsl-clippy.toml`:
  ```toml
  [rules.excess-interpolators]
  slot-budget = 12
  ```

## Fix availability

**none** — Reducing interpolator slot count requires semantic decisions about packing strategy (which scalar pairs share a vec4), which attributes can be reconstructed in the pixel shader instead of interpolated (e.g., bitangent from normal x tangent), and which can be moved to root constants or a structured buffer indexed by `SV_PrimitiveID`. These choices cross the vertex/pixel boundary and require human verification of numerical equivalence and bandwidth trade-offs.

## See also

- Related rule: [`nointerpolation-mismatch`](nointerpolation-mismatch.md) — flat-meaning attributes that pay for barycentric interpolation
- Related rule: [`oversized-cbuffer`](oversized-cbuffer.md) — analogous resource-budget rule for the constant-data path
- AMD GPUOpen: "Attribute interpolation on RDNA" — LDS layout for parameter cache
- D3D12 SM6.x reference: maximum input attribute counts per shader stage
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/excess-interpolators.md)

*© 2026 NelCit, CC-BY-4.0.*
