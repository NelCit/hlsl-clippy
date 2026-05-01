---
id: oversized-ray-payload
category: dxr
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
---

# oversized-ray-payload

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Ray payload structs — the `inout` parameter on `closesthit`, `anyhit`, and `miss` shaders, and the corresponding payload argument to `TraceRay` — whose total size exceeds a configurable byte threshold (default 32 bytes). The rule walks the struct definition, sums the size of each field after HLSL packing rules (16-byte vector alignment, scalar-tail packing), and reports the offending struct together with the entry points that consume it. The rule additionally flags payloads whose declared `[payload]` size attribute (where present) exceeds the threshold even if individual fields appear small after packing.

## Why it matters on a GPU

The DXR runtime stores the payload in a per-lane spill region — the same ray stack that backs `live-state-across-traceray` spills. Every byte of payload is written by the caller before traversal and read back by the caller after the closest-hit, any-hit, or miss shader returns. On NVIDIA Turing and Ada Lovelace, the RT cores execute traversal independently of the SM, but the payload itself lives in the SM's local memory and is touched by the streaming multiprocessor on every shader transition. On AMD RDNA 2/3, the Ray Accelerators perform BVH traversal but the payload sits in scratch backed by the vector memory hierarchy; each 16-byte payload chunk costs a cache-line round trip on the spill and refill.

Recursion compounds the cost. A path tracer that recurses 4 bounces deep with a 64-byte payload writes 256 bytes per lane per pixel into the ray stack just for payload state — before any live-across-trace spills are accounted for. Across a 1080p frame at 64 lanes per wave, the payload traffic alone is roughly 2 MB per recursion level per frame, which sits squarely on the L2-to-VRAM path and steals bandwidth from texture and BVH fetches. Intel Xe-HPG's RT units exhibit a similar pattern: payload spill is a shared-cache transaction, and oversized payloads visibly reduce the achievable rays/sec figure on Battlemage and Arc Alchemist.

The portable design heuristic, codified by both NVIDIA's and AMD's DXR best-practice guides, is to keep payloads at or below 32 bytes for hot paths (shadow / AO / primary) and to allow up to 64 bytes only for terminal closest-hit shaders that do not recurse. Beyond 64 bytes, the spill cost typically dominates the work performed by the hit shader. Splitting a fat payload into a "small in-flight" payload plus a side-buffer indexed by `RayIndex()` is the canonical fix when the data genuinely must persist across the trace.

## Examples

### Bad

```hlsl
// 96 bytes — far above the 32-byte default threshold.
struct FatPayload {
    float3 radiance;       // 12 bytes
    float3 throughput;     // 12 bytes (+4 pad to 16-byte boundary)
    float3 worldPos;       // 12 bytes
    float3 worldNormal;    // 12 bytes (+4 pad)
    float4 debugColor;     // 16 bytes
    uint   randomState;    // 4 bytes
    uint   bounceFlags;    // 4 bytes
    float  hitT;           // 4 bytes
    float  pdf;            // 4 bytes
};

[shader("raygeneration")]
void RayGen() {
    FatPayload payload = (FatPayload)0;
    RayDesc ray = MakePrimaryRay();
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
}
```

### Good

```hlsl
// 24 bytes — radiance + throughput + hitT only; debug data goes side-band.
struct LeanPayload {
    float3 radiance;       // 12 bytes
    float  hitT;           // 4 bytes
    uint2  packedThroughput; // 8 bytes — RGB10A2 or two halves; unpack on read
};

// Side-buffer for debug / less-hot state, indexed by DispatchRaysIndex().
RWStructuredBuffer<DebugPerRay> g_DebugSideBand : register(u3);
```

## Options

- `max-bytes` (integer, default: `32`) — payload size threshold in bytes. Set to `64` to allow medium payloads in projects that have measured the trade-off and accepted it.
- `recursive-max-bytes` (integer, default: `32`) — stricter threshold applied when the entry point appears in a recursive trace chain (closest-hit that itself calls `TraceRay`).

## Fix availability

**suggestion** — The rule reports the offending fields ranked by byte cost and suggests candidates for removal or side-buffer migration. The actual restructuring requires understanding which fields are read on the caller side after the trace returns; the fix is always manual.

## See also

- Related rule: [live-state-across-traceray](live-state-across-traceray.md) — caller-side spill that compounds payload cost
- Related rule: [missing-accept-first-hit](missing-accept-first-hit.md) — shadow-ray payloads should be near-empty (one bool)
- Related rule: [recursion-depth-not-declared](recursion-depth-not-declared.md) — recursion multiplies payload spill cost
- DXR specification: payload size constraints and `[payload]` attribute in the DirectX Raytracing spec
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/oversized-ray-payload.md)

*© 2026 NelCit, CC-BY-4.0.*
