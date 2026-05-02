---
id: hitobject-stored-in-memory
category: ser
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# hitobject-stored-in-memory

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `dx::HitObject` value (the SM 6.9 Shader Execution Reordering type) stored into any memory location: a `groupshared` declaration, a UAV write (`StructuredBuffer<dx::HitObject>` or `RWByteAddressBuffer.Store`), a return slot of a non-inlined function, or a globally-scoped variable. The SER specification (HLSL proposal 0027) restricts `dx::HitObject` to register-only lifetimes inside an inlined call chain rooted at a raygeneration shader. Reflection-aware analysis identifies the type via Slang's HLSL frontend and walks the assignment / return / store sites.

## Why it matters on a GPU

`dx::HitObject` represents a deferred ray-tracing hit that has already executed traversal but has not yet been dispatched to its closest-hit / any-hit / miss shader. On NVIDIA Ada Lovelace (the launch IHV for SER) the HitObject lives in a per-lane register file slice that the RT cores own jointly with the SM; on AMD RDNA 3/4 (when SER ships there) the same lifetime constraint applies. Storing a HitObject in memory is meaningless because the runtime cannot reconstruct the per-lane RT-core state from a flat byte representation — there is no canonical layout, and the spec deliberately leaves it implementation-defined.

The HLSL spec marks the operation as undefined behaviour. DXC emits an error in most forms; the Slang compiler emits an error; runtime drivers do not attempt to honour the store. The reasoning is simple — if the store were allowed, the runtime would have to spill the entire RT-core scoreboard to VRAM on every store and refill it on every load, which would obliterate the perf advantage SER exists to deliver.

Surfacing the violation at lint time names the offending storage class and points the author at the SER programming-model rule. The fix is structural: keep HitObject values in registers and inline the call chain that produces them, or use `dx::MaybeReorderThread` to reorder lanes around the HitObject before invoking it.

## Examples

### Bad

```hlsl
// Storing HitObject in groupshared — UB; spec violation.
groupshared dx::HitObject s_hits[64];   // ERROR: HitObject must be register-only

[shader("raygeneration")]
void RayGen() {
    uint laneId = WaveGetLaneIndex();
    RayDesc ray = MakePrimaryRay();
    s_hits[laneId] = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                             0, 1, 0, ray, /*payload*/);
    GroupMemoryBarrierWithGroupSync();
    s_hits[laneId].Invoke(/*payload*/);
}
```

### Good

```hlsl
// Keep HitObject in a register; reorder before invoke.
[shader("raygeneration")]
void RayGen() {
    RayDesc ray = MakePrimaryRay();
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, ray, /*payload*/);
    dx::MaybeReorderThread(hit);
    hit.Invoke(/*payload*/);
}
```

## Options

none

## Fix availability

**none** — Eliminating the store requires either inlining the call chain or restructuring the work so the HitObject's lifetime stays in registers; both are authorial decisions.

## See also

- Related rule: [maybereorderthread-outside-raygen](maybereorderthread-outside-raygen.md) — analogous SER programming-model rule
- Related rule: [hitobject-construct-outside-allowed-stages](hitobject-construct-outside-allowed-stages.md) — HitObject construction outside raygen
- Related rule: [hitobject-passed-to-non-inlined-fn](hitobject-passed-to-non-inlined-fn.md) — HitObject passed across non-inlined call boundary
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/hitobject-stored-in-memory.md)

*© 2026 NelCit, CC-BY-4.0.*
