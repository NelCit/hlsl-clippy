---
id: omm-rayquery-force-2state-without-allow-flag
category: opacity-micromaps
severity: error
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# omm-rayquery-force-2state-without-allow-flag

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `RayQuery<RAY_FLAG_FORCE_OMM_2_STATE>` template instantiation, or a `TraceRayInline` call with the equivalent runtime ray flag, in a shader that does not also set `RAY_FLAG_ALLOW_OPACITY_MICROMAPS` somewhere on the trace. The DXR 1.2 Opacity Micromap (OMM) specification requires both flags to coexist — `FORCE_OMM_2_STATE` is meaningful only when OMM is allowed in the first place. Constant-folding the `RAY_FLAG_*` argument at the call site makes the check straightforward.

## Why it matters on a GPU

DXR 1.2's Opacity Micromap feature exposes per-triangle opacity tables that the BVH traversal hardware on every supporting IHV (NVIDIA Ada Lovelace, AMD RDNA 4, Intel Xe-HPG with the OMM extension) can evaluate without invoking an any-hit shader. Two ray flags govern the OMM path:

- `RAY_FLAG_ALLOW_OPACITY_MICROMAPS`: tells the traversal hardware that the BVH may have OMM blocks attached to its primitives and that consulting them is permitted.
- `RAY_FLAG_FORCE_OMM_2_STATE`: collapses the OMM's four states (opaque, transparent, unknown-opaque, unknown-transparent) into two (opaque vs. transparent), turning every "unknown" into the opposite of the trace's coverage intent.

The two-state collapse is meaningful only when OMM is consulted at all. Setting `FORCE_OMM_2_STATE` without the corresponding `ALLOW_OPACITY_MICROMAPS` flag is undefined behaviour: the hardware does not consult the OMM blocks (because the allow flag is missing), so the force-2-state instruction has nothing to act on, and the spec leaves the hardware free to either silently ignore both flags or to fail the trace. The DXC validator catches the simplest combinations; reflection-aware constant-folding catches the rest.

The fix is a single OR into the ray-flag argument. The diagnostic emits the candidate rewrite as a comment.

## Examples

### Bad

```hlsl
// Force-2-state without allow-OMM — UB.
RaytracingAccelerationStructure g_BVH : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    RayQuery<RAY_FLAG_FORCE_OMM_2_STATE> q;  // missing ALLOW_OPACITY_MICROMAPS
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeRay(tid));
    /* ... */
}
```

### Good

```hlsl
// Both flags coexist — well-defined.
RaytracingAccelerationStructure g_BVH : register(t0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    RayQuery<RAY_FLAG_FORCE_OMM_2_STATE | RAY_FLAG_ALLOW_OPACITY_MICROMAPS> q;
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeRay(tid));
    /* ... */
}
```

## Options

none

## Fix availability

**suggestion** — `--fix` ORs `RAY_FLAG_ALLOW_OPACITY_MICROMAPS` into the existing `RayQuery<...>` template-flag argument. The rewrite does not duplicate the existing expression — it appends ` | RAY_FLAG_ALLOW_OPACITY_MICROMAPS` to the end of the flag list — so it is side-effect-safe regardless of how the original flag bundle was assembled. The fix is marked `machine_applicable = false` because adding the allow flag changes the trace's semantics: the developer must confirm the BVH actually has OMM blocks attached before accepting the fix in bulk.

## See also

- Related rule: [omm-allocaterayquery2-non-const-flags](omm-allocaterayquery2-non-const-flags.md) — companion OMM rule
- Related rule: [omm-traceray-force-omm-2state-without-pipeline-flag](omm-traceray-force-omm-2state-without-pipeline-flag.md) — companion OMM rule for pipeline traces
- Related rule: [missing-ray-flag-cull-non-opaque](missing-ray-flag-cull-non-opaque.md) — ray-flag tightening on a different axis
- HLSL specification: [proposal 0024 Opacity Micromaps](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0024-opacity-micromaps.md)
- Companion blog post: [opacity-micromaps overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/omm-rayquery-force-2state-without-allow-flag.md)

*© 2026 NelCit, CC-BY-4.0.*
