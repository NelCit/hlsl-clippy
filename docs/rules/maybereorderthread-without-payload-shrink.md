---
id: maybereorderthread-without-payload-shrink
category: ser
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
---

# maybereorderthread-without-payload-shrink

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `dx::MaybeReorderThread(...)` call whose surrounding payload struct contains live state that is *not read* after the reorder, i.e., values written before the reorder, not consumed inside the reorder's downstream invocation, and not read after. The Phase 7 IR-level live-range analysis (shared with `live-state-across-traceray`) walks per-lane lifetimes across the reorder and identifies fields that are dead across the call.

## Why it matters on a GPU

SER's runtime spills the entire ray-payload at the reorder point: `MaybeReorderThread` reorganises lanes, and the per-lane state (the payload, plus any caller-side live registers) has to follow each lane to its new position. NVIDIA's Indiana Jones path-tracer case study quantified this: the reorder's spill traffic is proportional to live-state size, and the case study reported 10-25% perf gains by shrinking the payload from 64 bytes to 16 bytes around the reorder, even when the larger payload was needed before and after.

The pattern is "fat across the trace, lean across the reorder": fill the payload once before the trace, decode it into a small lean record before the reorder, run the reorder + invoke on the lean record, refill the fat payload after. The Phase 7 IR-level analysis identifies fields that can be migrated to a side-buffer indexed by `DispatchRaysIndex()` — the canonical fix.

The rule is suggestion-tier because the side-buffer migration changes the application's read pattern; the diagnostic ranks the offending fields by per-lane byte cost and emits the candidate refactor as a comment.

This is research-grade per ADR 0010's Phase 7 placement; the rule ships alongside the existing `live-state-across-traceray` once the IR-reader infrastructure lands.

## Examples

### Bad

```hlsl
// 96-byte payload spilled across the reorder; only `radiance` is read after.
struct FatPayload {
    float3 radiance;
    float3 worldPos;
    float3 worldNormal;
    float4 debugColor;
    uint   bounceFlags;
    float  pdf;
};

[shader("raygeneration")]
void RayGen() {
    FatPayload p = (FatPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, /*...*/, p);
    dx::MaybeReorderThread(hit);          // spills 96 bytes per lane
    hit.Invoke(p);
    g_Output[dispatchIndex] = p.radiance; // only radiance is read after
}
```

### Good

```hlsl
// 16-byte payload across the reorder; debug + worldPos / worldNormal in side-buffer.
struct LeanPayload {
    float3 radiance;
    float  pdf;
};

RWStructuredBuffer<DebugRecord> g_DebugSide : register(u3);

[shader("raygeneration")]
void RayGen() {
    LeanPayload p = (LeanPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, /*...*/, p);
    dx::MaybeReorderThread(hit);          // spills 16 bytes per lane
    hit.Invoke(p);
    g_Output[dispatchIndex] = p.radiance;
}
```

## Options

- `min-shrink-bytes` (integer, default: `16`) — minimum estimated savings before the rule fires.

## Fix availability

**suggestion** — The fix is a structural refactor (side-buffer migration). The diagnostic ranks the offending fields by per-lane byte cost.

## See also

- Related rule: [live-state-across-traceray](live-state-across-traceray.md) — companion live-state rule for `TraceRay`
- Related rule: [oversized-ray-payload](oversized-ray-payload.md) — companion DXR payload rule
- Related rule: [coherence-hint-redundant-bits](coherence-hint-redundant-bits.md) — companion SER perf rule
- Reference: [Indiana Jones SER live-state case study](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/maybereorderthread-without-payload-shrink.md)

*© 2026 NelCit, CC-BY-4.0.*
