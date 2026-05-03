---
id: missing-accept-first-hit
category: dxr
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# missing-accept-first-hit

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

## What it detects

`TraceRay` (or `RayQuery::TraceRayInline`) call sites whose ray-flags argument lacks `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` even though the caller does not actually use the closest-hit information. The rule performs IR-level liveness and use-def analysis on the payload after the trace returns: if every field of the payload that is read post-trace is either (a) written only by the miss shader, or (b) a single `bool`/`uint` "did we hit anything" flag, the rule fires. The intent inferred by the rule is that the trace is a visibility / shadow / occlusion query, not a true closest-hit lookup.

## Why it matters on a GPU

Without `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`, the BVH traversal must continue until every leaf node intersected by the ray has been tested, because any later intersection might be closer than the current candidate. With the flag set, traversal terminates the moment the first opaque hit is recorded — for shadow rays, this halves the average traversal cost in dense geometry and reduces it by far more in skybox-bounded scenes where the typical shadow ray passes through hundreds of empty BVH nodes before hitting an occluder.

The hardware impact is direct on every current DXR-capable architecture. NVIDIA Turing, Ampere, and Ada Lovelace RT cores execute BVH-box and triangle tests as fixed-function pipelines; each early-out reclaims one BVH-traversal slot for the next ray in the wave. AMD RDNA 2/3 Ray Accelerators are similarly bounded by the number of node and triangle tests per ray; vendor measurements on RDNA 3 show shadow-ray rates roughly 1.8x higher with the early-termination flag set on representative scenes (Crytek Sponza, Bistro Exterior). Intel Xe-HPG's RT units behave the same way: the flag converts an unbounded leaf scan into a bounded one.

The reason the flag is so often missing is historical: the DXR sample code shipped early on used `RAY_FLAG_NONE` everywhere and copy-paste propagated the omission. A modern shadow-ray formulation also benefits from `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER` — the closest-hit shader is never invoked because traversal ends at the first hit, but explicitly skipping it lets the driver omit the closest-hit shader binding from the hit-group lookup entirely. The combination `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER` is the canonical shadow-ray flag set and should be the lint-suggested fix.

## Examples

### Bad

```hlsl
// Shadow ray: only the miss shader writes anything we read; closesthit unused.
struct ShadowPayload { bool hit; };

[shader("miss")]
void ShadowMiss(inout ShadowPayload p) { p.hit = false; }

[shader("raygeneration")]
void ShadeWithShadow() {
    ShadowPayload sp; sp.hit = true;     // optimistic — assume occluded
    RayDesc ray = MakeShadowRay();
    // RAY_FLAG_NONE — traversal walks the entire BVH leaf set.
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, sp);
    if (!sp.hit) { /* lit */ }
}
```

### Good

```hlsl
[shader("raygeneration")]
void ShadeWithShadow() {
    ShadowPayload sp; sp.hit = true;
    RayDesc ray = MakeShadowRay();
    // First hit ends traversal; closest-hit shader skipped entirely.
    const uint flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
                     | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    TraceRay(Scene, flags, 0xFF, 0, 1, 0, ray, sp);
    if (!sp.hit) { /* lit */ }
}
```

## Options

- `payload-fields-threshold` (integer, default: `1`) — fire when the number of payload fields read after the trace is at most this value. Increase to be stricter (treat any small payload as a visibility query); decrease to `0` to require zero post-trace reads.
- `require-skip-closest-hit` (bool, default: `true`) — also flag the absence of `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER` when the closest-hit shader is provably unused.

## Fix availability

**suggestion** — The rule proposes adding `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` (and optionally `RAY_FLAG_SKIP_CLOSEST_HIT_SHADER`) to the flags argument. Because the flag changes which hit is reported when multiple intersections exist, the user must confirm that the trace is genuinely a visibility query before applying the fix.

## See also

- Related rule: [oversized-ray-payload](oversized-ray-payload.md) — shadow rays should pair with a 4-byte payload
- Related rule: [recursion-depth-not-declared](recursion-depth-not-declared.md) — shadow rays should not recurse; setting `MaxTraceRecursionDepth = 1` for shadow PSOs is a related win
- DXR specification: `D3D12_RAY_FLAG` enumeration in the DirectX Raytracing spec
- NVIDIA DXR best-practices: "Shadow Rays" section
- Companion blog post: [dxr overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/missing-accept-first-hit.md)

*© 2026 NelCit, CC-BY-4.0.*
