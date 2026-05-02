---
id: coherence-hint-redundant-bits
category: ser
severity: warn
applicability: machine-applicable
since-version: v0.5.0
phase: 4
---

# coherence-hint-redundant-bits

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `dx::MaybeReorderThread(hit, coherenceHint, hintBits)` call whose `hintBits` argument is larger than necessary to express the actual coherence-hint value, or whose `coherenceHint` value has bits set above bit `hintBits-1`. The Phase 4 bit-range analysis tracks the constant or affine bound on the hint expression and compares it with the declared bit count.

## Why it matters on a GPU

`MaybeReorderThread`'s coherence hint is the developer-supplied bucketing key the runtime uses to coalesce divergent lanes. NVIDIA Ada Lovelace's SER scheduler uses up to 16 bits of hint by default; AMD RDNA 4's SER implementation (when shipped) and the Vulkan `VK_EXT_ray_tracing_invocation_reorder` extension expose the same surface. The driver applies the hint's `hintBits` mask to the `coherenceHint` value and uses the masked bits to bucket lanes — fewer bits means a coarser bucketing and a potentially less effective reorder.

Declaring more bits than the hint actually uses costs nothing functionally — the unused bits are zero — but it tells the driver to *try* to coalesce on bits the application does not actually populate, which can dilute the bucketing on workloads where the hint's effective entropy is concentrated in the low bits. NVIDIA's SER perf blog calls this out specifically: a 4-bit hint (16 buckets) often coalesces better than a 16-bit hint that only fills 4 bits because the scheduler does not have to factor the dead bits into its grouping decision.

The fix is to tighten the `hintBits` argument to match the actual entropy of the hint. The rule is machine-applicable when the bit-range analysis can prove the tight bound; otherwise it is suggestion-tier with the candidate value emitted as a comment.

## Examples

### Bad

```hlsl
// Hint value is hitGroup index (0..15) — uses 4 bits; declared 16.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    uint hg = hit.GetShaderTableIndex(); // proven 0..15 by bit-range analysis
    dx::MaybeReorderThread(hit, hg, /*hintBits*/ 16);   // dilutes scheduler
    hit.Invoke(p);
}
```

### Good

```hlsl
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    uint hg = hit.GetShaderTableIndex();
    dx::MaybeReorderThread(hit, hg, /*hintBits*/ 4);    // tight match
    hit.Invoke(p);
}
```

## Options

none

## Fix availability

**machine-applicable** — When the bit-range analysis has a tight bound, `hlsl-clippy fix` rewrites the literal `hintBits` argument. When the bound is approximate, the diagnostic emits the candidate as a comment.

## See also

- Related rule: [coherence-hint-encodes-shader-type](coherence-hint-encodes-shader-type.md) — companion coherence-hint rule
- Related rule: [maybereorderthread-without-payload-shrink](maybereorderthread-without-payload-shrink.md) — companion SER perf rule
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Reference: [NVIDIA SER perf blog](https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coherence-hint-redundant-bits.md)

*© 2026 NelCit, CC-BY-4.0.*
