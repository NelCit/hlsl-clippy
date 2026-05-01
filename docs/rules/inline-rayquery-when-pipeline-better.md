---
id: inline-rayquery-when-pipeline-better
category: dxr
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# inline-rayquery-when-pipeline-better

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Use of `RayQuery<>` (inline ray queries, SM 6.5+) in shaders where the workload characteristics make pipeline `TraceRay` the better choice — or, conversely, use of pipeline `TraceRay` for shaders where inline ray queries would be faster. The heuristic flags inline RQ inside pixel or compute shaders that traverse rays with high candidate counts (many alpha-tested any-hit calls), and flags pipeline RQ for shaders with simple shadow / AO / occlusion queries that have no any-hit work and a single uniform miss case.

## Why it matters on a GPU

DXR offers two ray-tracing programming models. Pipeline ray tracing (`TraceRay` plus `[shader("anyhit"|"closesthit"|"miss"|"intersection")]` entry points) routes every traversal event through the shader table, allowing different geometry types to bind different shaders and enabling hardware Shader Execution Reordering (SER on NVIDIA Ada / Blackwell) to regroup divergent rays for SIMD efficiency. Inline ray queries (`RayQuery<RAY_FLAGS>` plus `Proceed()` / `CommittedXxx()` accessors) inline the entire traversal into the calling shader's body, eliminating the shader-table indirection but giving up the SER regrouping and forcing the caller to handle every traversal event in its own register file.

The throughput trade-off depends on three factors. First, candidate-hit count: shaders that traverse foliage, hair, or layered alpha-test geometry execute many any-hit-equivalent calls per ray. Pipeline RT lets these any-hits run as a separate compact shader (small register footprint, good SIMD packing); inline RQ forces every alpha test to inhabit the caller's register file, inflating VGPR pressure and crushing occupancy. Second, divergence pattern: rays that scatter widely (reflections, GI bounces) benefit from SER on Ada / Blackwell, which inline RQ cannot use. Third, payload weight: pipeline RT has a fixed cost to marshal a payload across the trace boundary, so trivially small payloads (a single bool for shadow visibility) lose ground; inline RQ has zero payload cost.

Empirically, on the Ada generation: inline RQ is 20-50% faster for simple shadow / ambient-occlusion queries with no alpha test and one trivial miss case (RT shadow passes, contact shadow rays). Pipeline RT is 30-60% faster for path-traced GI bounces, foliage shadows with > 4 alpha layers, and any workload with high SER opportunity. On RDNA 2/3 the same direction holds with smaller margins (10-30% in either direction) because RDNA does not have SER. The wrong choice can leave 20-50% of ray throughput on the table; the right choice often requires measuring both forms on representative content.

## Examples

### Bad

```hlsl
RaytracingAccelerationStructure Scene : register(t0);

// Foliage shadow pass with alpha-test geometry — inline RQ forces every
// any-hit-equivalent alpha test into the caller's register file.
float foliage_shadow(float3 origin, float3 dir, float tmax) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rq;
    RayDesc r;  r.Origin = origin;  r.Direction = dir;
    r.TMin = 0.01;  r.TMax = tmax;
    rq.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, r);
    while (rq.Proceed()) {
        if (rq.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            float2 uv = ResolveCandidateUV(rq);
            // Alpha test inhabits the caller's VGPRs — kills occupancy.
            if (FoliageAtlas.SampleLevel(Samp, uv, 0).a > 0.5) {
                rq.CommitNonOpaqueTriangleHit();
            }
        }
    }
    return rq.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}
```

### Good

```hlsl
// For foliage shadows, dispatch via pipeline RT so the alpha test runs
// in a separate compact any-hit shader with its own register file.
struct ShadowPayload { float visible; };

[shader("raygeneration")]
void rg_foliage_shadow() {
    uint2 px = DispatchRaysIndex().xy;
    RayDesc r = MakeShadowRay(px);
    ShadowPayload pl;  pl.visible = 1.0;
    TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
             0xFF, 0, 1, 0, r, pl);
    Out[px] = float4(pl.visible.xxx, 1);
}

[shader("anyhit")]
void ah_foliage(inout ShadowPayload pl, in BuiltInTriangleIntersectionAttributes a) {
    float2 uv = ResolveUV(a);
    if (FoliageAtlas.SampleLevel(Samp, uv, 0).a < 0.5) IgnoreHit();
}
```

## Options

- `prefer` (string, default: `"auto"`) — `"inline"`, `"pipeline"`, or `"auto"`. With `"auto"`, the rule applies the heuristic above.

## Fix availability

**suggestion** — Switching between inline and pipeline RT is a substantial refactor that affects shader entry points, the shader table, and the C++ pipeline-state setup. The diagnostic recommends the alternative model based on the candidate-count heuristic and SER availability flags from the target SM.

## See also

- Related rule: [tracerray-conditional](tracerray-conditional.md) — `TraceRay` placement and live-state spill
- Related rule: [anyhit-heavy-work](anyhit-heavy-work.md) — heavy any-hit work
- NVIDIA developer blog: SER on Ada — when pipeline RT wins
- Microsoft DirectX docs: Inline Ray Queries (SM 6.5)
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/inline-rayquery-when-pipeline-better.md)

*© 2026 NelCit, CC-BY-4.0.*
