---
id: hitobject-invoke-after-recursion-cap
category: ser
severity: error
applicability: none
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# hitobject-invoke-after-recursion-cap

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `dx::HitObject::Invoke(...)` call reachable from a closest-hit shader chain whose nominal recursion depth exceeds the pipeline's `MaxTraceRecursionDepth`. The Phase 4 call-graph + recursion-budget analysis walks the trace chain (raygen -> closest-hit -> potentially another `Invoke` -> closest-hit -> ...) and accumulates the depth.

## Why it matters on a GPU

The DXR pipeline's `D3D12_RAYTRACING_PIPELINE_CONFIG.MaxTraceRecursionDepth` declares an upper bound on how deep the trace stack can grow. The runtime uses this bound to size the per-lane ray stack, which is shared between `TraceRay`-style recursion and `HitObject::Invoke`-style deferred invocation. On NVIDIA Ada Lovelace, AMD RDNA 3/4, and Intel Xe-HPG (with the OMM extension), exceeding the declared depth at runtime is undefined behaviour: the hardware may corrupt adjacent stack frames, fault, or silently truncate.

`HitObject::Invoke` consumes a depth slot just like `TraceRay`. The Phase 4 analysis traces the call graph from raygen forward, treating each `TraceRay` and each `Invoke` as a depth-1 step, and fires when any path's accumulated depth exceeds the declared `MaxTraceRecursionDepth`. The reflection-aware front matter pulls the recursion cap from the pipeline subobject if available; when not, the rule defaults to a configurable threshold.

The fix is either to raise the pipeline's `MaxTraceRecursionDepth` (with the corresponding ray-stack sizing trade-off — every depth slot costs scratch memory per lane) or to flatten the chain by using `RayQuery` for nested traces. The diagnostic names the path through the call graph that hits the cap.

## Examples

### Bad

```hlsl
// Pipeline declares MaxTraceRecursionDepth = 1.
// closesthit:OnHit calls Invoke on a deeper hit -> depth 2 -> over the cap.
[shader("closesthit")]
void OnHit(inout RayPayload p, BuiltInTriangleIntersectionAttributes attr) {
    dx::HitObject deeper = dx::HitObject::FromRayQuery(/* ... */);
    deeper.Invoke(p);   // depth 2; UB
}
```

### Good

```hlsl
// Either raise MaxTraceRecursionDepth in the pipeline subobject, or
// flatten the chain using RayQuery for the inner trace.
[shader("closesthit")]
void OnHit(inout RayPayload p, BuiltInTriangleIntersectionAttributes attr) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeFollowupRay());
    q.Proceed();
    p.acc += q.CommittedTriangleHitT();   // no extra depth slot
}
```

## Options

none

## Fix availability

**none** — Either pipeline-subobject change or structural refactor; both are authorial.

## See also

- Related rule: [recursion-depth-not-declared](recursion-depth-not-declared.md) — companion DXR validation rule
- Related rule: [hitobject-passed-to-non-inlined-fn](hitobject-passed-to-non-inlined-fn.md) — companion SER programming-model rule
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/hitobject-invoke-after-recursion-cap.md)

*© 2026 NelCit, CC-BY-4.0.*
