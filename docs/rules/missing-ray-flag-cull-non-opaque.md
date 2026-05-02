---
id: missing-ray-flag-cull-non-opaque
category: dxr
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# missing-ray-flag-cull-non-opaque

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0007)*

## What it detects

`TraceRay(...)` or `RayQuery::TraceRayInline(...)` calls whose ray-flag argument does not include `RAY_FLAG_CULL_NON_OPAQUE` in a context where the bound any-hit shader is empty (returns immediately, only calls `IgnoreHit()`/`AcceptHitAndEndSearch()` unconditionally) or where reflection shows no any-hit shader is bound to the relevant hit groups. The rule reads the constant ray-flag value via tree-sitter constant-folding and uses Slang reflection to enumerate the hit groups in the pipeline subobject.

## Why it matters on a GPU

DXR traversal on every modern IHV (NVIDIA Turing/Ada Lovelace RT cores, AMD RDNA 2/3 Ray Accelerators, Intel Xe-HPG RTU) splits BVH leaf processing into two paths: the *opaque path*, where the leaf primitive is accepted directly by the traversal hardware, and the *non-opaque path*, where the hardware suspends traversal, returns to the SIMT engine, runs the any-hit shader, and resumes. The opaque path stays inside the RT hardware end-to-end; the non-opaque path costs a full shader invocation per leaf hit, including the wave reformation and the trip back through the scheduler. NVIDIA's Ada RT-core whitepaper measures the per-non-opaque-hit cost at roughly 30-60 ALU cycles of overhead on top of the any-hit shader's own work, even when the any-hit body is empty.

`RAY_FLAG_CULL_NON_OPAQUE` instructs the traversal hardware to skip non-opaque geometry entirely. When the application has no real any-hit logic — common in shadow rays and primary-visibility rays where alpha-test is not in scope — this flag turns every potentially-non-opaque BVH visit into a no-op inside the RT cores. AMD's RDNA 2/3 Ray Accelerator behaves the same way: the flag is consumed by the BVH walker and prunes the suspend/resume round trip. The reported speedups on shadow-ray heavy workloads (Cyberpunk 2077, Portal RTX) range from 5% to 15% of total RT time.

The complement is true: when a hit group genuinely needs alpha-tested geometry, the flag must not be set. The rule fires only when reflection or AST scanning shows the any-hit body is dead, which is the safe direction.

## Examples

### Bad

```hlsl
// Shadow ray: any-hit shader is unbound (or empty); flag is missing.
RaytracingAccelerationStructure g_BVH : register(t0);

[shader("raygeneration")]
void RayGen() {
    RayDesc ray = MakeShadowRay();
    ShadowPayload payload = { 1.0f };
    // No CULL_NON_OPAQUE — every alpha-tested leaf bounces back to SIMT.
    TraceRay(g_BVH, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF,
             1, 1, 1, ray, payload);
}
```

### Good

```hlsl
// Same shadow ray with CULL_NON_OPAQUE. Traversal stays on the RT cores.
RaytracingAccelerationStructure g_BVH : register(t0);

[shader("raygeneration")]
void RayGen() {
    RayDesc ray = MakeShadowRay();
    ShadowPayload payload = { 1.0f };
    TraceRay(g_BVH,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
             0xFF, 1, 1, 1, ray, payload);
}
```

## Options

none

## Fix availability

**suggestion** — The fix is a single-bit OR into the ray-flag constant, but the linter cannot prove safety without confirming that the application doesn't rely on alpha-tested geometry being visited by this trace. The diagnostic emits the suggested rewrite as a comment.

## See also

- Related rule: [anyhit-heavy-work](anyhit-heavy-work.md) — heavy any-hit shaders are the opposite footgun
- Related rule: [missing-accept-first-hit](missing-accept-first-hit.md) — shadow rays should also accept the first hit
- Related rule: [tracerray-conditional](tracerray-conditional.md) — `TraceRay` inside non-uniform CF
- DXR specification: Ray flags table (`RAY_FLAG_CULL_NON_OPAQUE`)
- Companion blog post: [dxr overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/missing-ray-flag-cull-non-opaque.md)

*© 2026 NelCit, CC-BY-4.0.*
