---
id: maybereorderthread-outside-raygen
category: ser
severity: error
applicability: none
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# maybereorderthread-outside-raygen

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A call to `dx::MaybeReorderThread(...)` from any DXR stage other than raygeneration. The SM 6.9 SER specification restricts `MaybeReorderThread` to raygen because reordering must happen *before* the per-lane work that the reorder is meant to coalesce. Reflection identifies the entry-point stage; the call-graph walk fires when a `MaybeReorderThread` invocation is reachable from a closest-hit, any-hit, miss, callable, or compute entry.

## Why it matters on a GPU

SER's value comes from coalescing divergent lanes within a wave around a common shader execution. On NVIDIA Ada Lovelace's RT subsystem, the reorder happens at a hardware-scoped boundary that the runtime can only manipulate at the raygen invocation point — once a wave has dispatched into a closest-hit shader, the lane mapping is committed and reordering would have to spill and re-form the wave, which costs more than it saves. AMD RDNA 4 (when SER ships there) and the cross-platform Vulkan equivalent (`VK_EXT_ray_tracing_invocation_reorder`) exhibit the same constraint for the same hardware reason.

The DXR runtime treats `MaybeReorderThread` outside raygen as a hard validation failure. DXC produces a precise error; the runtime rejects the PSO. As with the other SER rules, surfacing this at lint time replaces a "your PSO does not link" runtime message with a source-located diagnostic that names both the call site and the shader stage.

The fix is structural: hoist the `MaybeReorderThread` to the raygen entry, before the `HitObject::TraceRay`-then-`Invoke` sequence the reorder is meant to optimise.

## Examples

### Bad

```hlsl
// Closest-hit shader cannot call MaybeReorderThread.
[shader("closesthit")]
void OnHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    dx::HitObject hit = dx::HitObject::FromRayQuery(/* ... */);
    dx::MaybeReorderThread(hit); // ERROR: not allowed outside raygen
    hit.Invoke(payload);
}
```

### Good

```hlsl
// MaybeReorderThread lives in raygen, between TraceRay and Invoke.
[shader("raygeneration")]
void RayGen() {
    RayDesc ray = MakePrimaryRay();
    RayPayload payload = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, ray, payload);
    dx::MaybeReorderThread(hit);
    hit.Invoke(payload);
}
```

## Options

none

## Fix availability

**none** — The fix requires moving the reorder to raygen and refactoring the surrounding hit-shader logic; both are authorial.

## See also

- Related rule: [hitobject-stored-in-memory](hitobject-stored-in-memory.md) — companion SER programming-model rule
- Related rule: [hitobject-construct-outside-allowed-stages](hitobject-construct-outside-allowed-stages.md) — HitObject construction outside allowed stages
- Related rule: [maybereorderthread-without-payload-shrink](maybereorderthread-without-payload-shrink.md) — perf rule on the same intrinsic
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/maybereorderthread-outside-raygen.md)

*© 2026 NelCit, CC-BY-4.0.*
