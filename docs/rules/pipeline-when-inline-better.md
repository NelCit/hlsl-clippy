---
id: pipeline-when-inline-better
category: dxr
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# pipeline-when-inline-better

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0007)*

## What it detects

A full DXR pipeline-`TraceRay` call from a stage that would do better with an inline `RayQuery`. The rule fires on `TraceRay` invocations whose payload is empty or single-scalar, whose `MissShaderIndex` and `RayContributionToHitGroupIndex` resolve to "shadow-ray" hit groups (any-hit / closest-hit shaders that only set a single bool), and whose call-site stage already runs in compute or pixel — both stages where a `RayQuery<RAY_FLAG_*>` inline traversal pays no shader-table indirection. The companion rule `inline-rayquery-when-pipeline-better` detects the opposite direction.

## Why it matters on a GPU

DXR exposes two traversal styles. *Pipeline ray tracing* uses `TraceRay` and a state-object full of hit-group and miss shaders; the runtime's scheduler dispatches the right shader through the shader-binding-table indirection on every hit, and SER (SM 6.9) layers on top to coalesce the divergent dispatches. *Inline ray tracing* uses `RayQuery` and runs the traversal as a method call inside the caller — no shader table, no hit-group dispatch, no payload spill across the trace. Both NVIDIA Ada Lovelace and AMD RDNA 3 expose the same RT-core hardware to both modes; the difference is entirely in the surrounding scheduling.

For a shadow ray — fire ray, get back "hit" / "no hit", record into the visibility buffer — the inline form is strictly cheaper on every IHV. The pipeline form pays a shader-table indirection per hit, a payload spill / refill across the trace, and (on RDNA 2/3) a wave reformation when the hit groups vary across the wave. NVIDIA's RT-core guidance and AMD's RDNA 2 ray-tracing best-practices both recommend `RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE>` over `TraceRay` for shadow / occlusion / AO probes. Reported wins on shadow-pass workloads land in the 10-30% range when the conversion is correct.

The rule is a suggestion-tier check: getting the conversion wrong (using inline for a hit pattern that genuinely benefits from hit-group divergence and SER coalescing) is also a perf loss, just in the opposite direction. The diagnostic surfaces the candidate trace and asks the author to verify.

## Examples

### Bad

```hlsl
// Single-bit shadow trace via the pipeline path — pays shader-table cost.
RaytracingAccelerationStructure g_BVH : register(t0);

struct ShadowPayload { float visibility; };

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    RayDesc ray = MakeShadowRay(tid);
    ShadowPayload p = { 1.0f };
    TraceRay(g_BVH,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
             0xFF, 1, 1, 1, ray, p);
    g_VisibilityUAV[tid.xy] = p.visibility;
}
```

### Good

```hlsl
// Inline RayQuery — no shader table, no payload spill.
RaytracingAccelerationStructure g_BVH : register(t0);

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    RayDesc ray = MakeShadowRay(tid);
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE> q;
    q.TraceRayInline(g_BVH, 0, 0xFF, ray);
    q.Proceed();
    float vis = (q.CommittedStatus() == COMMITTED_NOTHING) ? 1.0f : 0.0f;
    g_VisibilityUAV[tid.xy] = vis;
}
```

## Options

none

## Fix availability

**suggestion** — The rewrite swaps a `TraceRay` call for a `RayQuery` block, which changes the surrounding control flow and is not a textual substitution. The diagnostic emits a candidate replacement as a comment.

## See also

- Related rule: [inline-rayquery-when-pipeline-better](inline-rayquery-when-pipeline-better.md) — the opposite-direction sibling rule
- Related rule: [missing-ray-flag-cull-non-opaque](missing-ray-flag-cull-non-opaque.md) — ray-flag tightening for the same shadow-ray case
- DXR specification: Inline ray tracing (`RayQuery`) and pipeline ray tracing (`TraceRay`) trade-offs
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/pipeline-when-inline-better.md)

*© 2026 NelCit, CC-BY-4.0.*
