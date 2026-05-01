---
id: coherence-hint-encodes-shader-type
category: ser
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# coherence-hint-encodes-shader-type

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `dx::MaybeReorderThread(hit, coherenceHint, hintBits)` call whose `coherenceHint` expression is data-flow-tainted by `hit.IsHit()` or `hit.GetShaderTableIndex()`. The SER scheduler already groups lanes by hit-group / miss-vs-hit; encoding the same information in the user-supplied coherence hint duplicates work and can confuse the scheduler's bucketing heuristic. The Phase 4 taint analysis tracks `IsHit` and `GetShaderTableIndex` returns through arithmetic and conditional expressions.

## Why it matters on a GPU

The whole point of SER is that the driver's scheduler already knows how to coalesce lanes by their downstream shader (hit group / miss / etc.). NVIDIA Ada Lovelace's scheduler uses the HitObject's intrinsic data to pre-bucket lanes; AMD RDNA 4 / Vulkan SER extensions match. The `coherenceHint` argument is meant for *application-specific* axes the scheduler doesn't know about: which material is hit, which BVH instance was traversed, which payload bucket the lane belongs to. Encoding the shader-table index into the hint duplicates information the scheduler already has and forces it to factor the same axis twice into its bucketing — at best wasted work, at worst a worse final grouping than the no-hint baseline.

NVIDIA's SER guidance and the Indiana Jones path-tracer case study both call this out explicitly: pick a hint that the driver does *not* already know. Material ID is the canonical example (the driver doesn't know which material a shader-table-index-7 lane will sample); shader-table index is the canonical anti-example.

The taint analysis catches both direct uses (`hg = hit.GetShaderTableIndex(); reorder(hit, hg, 4)`) and indirect uses (`hg = hit.GetShaderTableIndex() & 0xF; reorder(hit, hg | (someFlag << 4), 5)` — still tainted because the low 4 bits are derived from the shader index). The diagnostic names the tainted expression and points the author at picking an application-specific axis.

## Examples

### Bad

```hlsl
// Hint encodes the shader-table index — duplicates the scheduler's own work.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    uint hg = hit.GetShaderTableIndex();
    dx::MaybeReorderThread(hit, hg, 4);     // scheduler-redundant hint
    hit.Invoke(p);
}
```

### Good

```hlsl
// Hint encodes material ID — application axis the scheduler doesn't know.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    uint matId = LookupMaterialId(hit);     // application-specific data
    dx::MaybeReorderThread(hit, matId, 6);
    hit.Invoke(p);
}
```

## Options

none

## Fix availability

**suggestion** — The hint replacement requires an application-side lookup; the diagnostic names the tainted source and asks for an application-specific axis.

## See also

- Related rule: [coherence-hint-redundant-bits](coherence-hint-redundant-bits.md) — companion coherence-hint rule
- Related rule: [maybereorderthread-without-payload-shrink](maybereorderthread-without-payload-shrink.md) — companion SER perf rule
- Reference: [Indiana Jones SER live-state case study](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coherence-hint-encodes-shader-type.md)

*© 2026 NelCit, CC-BY-4.0.*
