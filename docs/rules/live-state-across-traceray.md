---
id: live-state-across-traceray
category: memory
severity: warn
applicability: none
since-version: v0.7.0
phase: 7
---

# live-state-across-traceray

> **Pre-v0 status:** this rule is documented ahead of implementation. Detection
> requires dataflow analysis across the TraceRay call site in the compiled
> DXIL or via Slang's IR; it is not yet wired into the linter pipeline.

## What it detects

Local variables or intermediate computed values that are (a) assigned before a
`TraceRay` or `RayQuery::TraceRayInline` call and (b) read after the call
returns, making them live across the trace boundary. The rule fires when the
number of such live values — weighted by their register footprint in bytes —
exceeds a configurable byte threshold. It also fires for any individual value
larger than 16 bytes (one `float4`) that is live across a trace, because even
a single large live value materialises in the per-lane ray stack.

## Why it matters on a GPU

`TraceRay` is not an ordinary function call. The DXR execution model suspends
the calling shader and invokes intersection, any-hit, and closest-hit shaders
that may themselves call `TraceRay` recursively. The hardware cannot keep the
calling shader's register file live during traversal — the traversal may take
hundreds of cycles and would block the entire SIMD unit if registers were held.
Instead, the driver spills every value that the compiler determines is live
across the trace to a per-lane ray stack: a private VRAM allocation, typically
backed by the same scratch-memory infrastructure as local-array indexing. Each
spilled `float4` costs a 128-bit write before the trace and a 128-bit read
after it, with VRAM latencies (80-300 cycles per access on RDNA 3 and Turing).

For path-tracing shaders — the primary DXR workload — it is common to keep
world-space position, accumulated throughput, the BRDF evaluation result, and
random-state values all live across a bounce. A naive implementation can
easily accumulate 128-512 bytes of ray-stack state per lane per bounce. With
64 lanes per wave and 4-8 concurrent waves, the total ray-stack footprint runs
into tens of kilobytes per CU, saturating VRAM bandwidth and adding
round-trip memory latency to every bounce.

The correct fix is recompute-after-trace: instead of keeping an intermediate
computed value live across the trace, recompute it cheaply from inputs that
are already in the payload or from cbuffer data. For values that are genuinely
cheaper to preserve than to recompute, move them into the ray payload struct
(which DXR already spills as part of the payload mechanism). Moving state into
the payload avoids double-counting: the payload is always spilled by the
runtime; adding a value to it does not increase the ray-stack cost if the
payload was not at its size limit.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/raytracing.hlsl — HIT(tracerray-conditional)
// (live-state-across-traceray fires on similar patterns)
[shader("closesthit")]
void ClosestHit(inout BigPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    float3 dir = WorldRayDirection();
    // 'dir' is live before the trace — will spill to ray stack.
    if (dir.y > 0.0) {
        RayDesc ray;
        ray.Direction = reflect(dir, float3(0, 1, 0));
        ray.TMin = 0.001;
        ray.TMax = TMax;
        // 'dir' must survive TraceRay — spilled to per-lane ray stack.
        TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    }
    // 'dir' read here after trace returns — forces the spill.
}
```

### Good

```hlsl
// Recompute dir after the trace — WorldRayDirection() is a system value, not spilled.
[shader("closesthit")]
void ClosestHit(inout BigPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    float3 dir = WorldRayDirection();
    if (dir.y > 0.0) {
        RayDesc ray;
        ray.Direction = reflect(dir, float3(0, 1, 0));
        ray.TMin = 0.001;
        ray.TMax = TMax;
        TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
        // Recompute rather than spill — WorldRayDirection() does not spill.
        dir = WorldRayDirection();
    }
}

// Alternatively, move state into the payload (already spilled by DXR).
struct BetterPayload {
    float3 radiance;
    float  hitT;
    float3 throughput;  // moved here — not live across trace in the closesthit body
    uint   flags;
};
```

## Options

- `live-bytes-threshold` (integer, default: `64`) — fire if total live-across-
  trace state exceeds this many bytes per `TraceRay` site. Set to `0` to fire
  on any live state.

## Fix availability

**none** — The choice between recomputation and payload expansion requires
understanding the cost of each approach for the specific shader. The rule
identifies the spilled variables and their sizes; the fix is always manual.

## See also

- Related rule: [vgpr-pressure-warning](vgpr-pressure-warning.md) — ray-stack
  spill is related to but distinct from register-file pressure
- Related rule: [oversized-ray-payload](oversized-ray-payload.md) — payload
  struct size; moving live state into the payload compounds this rule
- Related rule: [tracerray-conditional](tracerray-conditional.md) — non-uniform
  control flow around TraceRay extends live ranges further
- DXR specification: "Shader Execution Reordering" and payload life-cycle in
  the DirectX Raytracing specification
- Companion blog post: [memory overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/live-state-across-traceray.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
