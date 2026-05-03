---
id: startvertexlocation-not-vs-input
category: wave-helper-lane
severity: error
applicability: none
since-version: v0.3.0
phase: 3
language_applicability: ["hlsl"]
---

# startvertexlocation-not-vs-input

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A use of the SM 6.8 `SV_StartVertexLocation` (or the analogous `SV_StartInstanceLocation`) system-value semantic anywhere other than as a vertex-shader input parameter. The SM 6.8 spec restricts these semantics to VS-input position only: they expose the per-draw `BaseVertexLocation` / `StartInstanceLocation` value the runtime passes to `Draw*` calls. Putting them on a VS output, a PS input, or a compute parameter is invalid. Slang reflection identifies the parameter's stage role; the rule fires on misplacement.

## Why it matters on a GPU

`SV_StartVertexLocation` is the per-draw constant the rasterizer broadcasts to all VS invocations: it is the `BaseVertexLocation` argument of `DrawIndexed*`, materialised as a per-vertex input. On NVIDIA Ada Lovelace and AMD RDNA 2/3, the value is delivered through the shader-input pipeline as a uniform-across-the-wave value; Intel Xe-HPG implements the same path. The hardware delivers it only at VS-input — the value has no defined meaning at any later stage because the rasterizer does not propagate it through the inter-stage parameter cache.

Putting the semantic on a VS output ("forward this to PS") or a PS input is a hard validator error: the semantic is not in the legal-IO table for those stages. DXC catches the simplest forms; the lint catches the rest, including the case where the semantic is on a struct member that is then passed across the VS->PS boundary.

The fix is to read the value at VS input and forward it explicitly through a `TEXCOORD` slot if PS needs it, or to skip it entirely if the per-draw value isn't actually consumed.

## Examples

### Bad

```hlsl
// SV_StartVertexLocation as a VS output is invalid.
struct VSOut {
    float4 pos     : SV_Position;
    uint   baseVtx : SV_StartVertexLocation;  // ERROR: not allowed on output
};

VSOut main(uint vtx : SV_VertexID, uint baseVtx : SV_StartVertexLocation) {
    VSOut o;
    o.pos     = float4(0, 0, 0, 1);
    o.baseVtx = baseVtx;
    return o;
}
```

### Good

```hlsl
// Read at VS input; forward via TEXCOORD if PS needs it.
struct VSOut {
    float4 pos    : SV_Position;
    uint   baseVtxFwd : TEXCOORD0;
};

VSOut main(uint vtx : SV_VertexID, uint baseVtx : SV_StartVertexLocation) {
    VSOut o;
    o.pos        = float4(0, 0, 0, 1);
    o.baseVtxFwd = baseVtx;
    return o;
}
```

## Options

none

## Fix availability

**none** — Restructuring the IO is authorial.

## See also

- Related rule: [excess-interpolators](excess-interpolators.md) — IO signature pressure
- HLSL specification: [SM 6.8 SV_StartVertexLocation](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_8.html)
- Companion blog post: [wave-helper-lane overview](../blog/wave-helper-lane-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/startvertexlocation-not-vs-input.md)

*© 2026 NelCit, CC-BY-4.0.*
