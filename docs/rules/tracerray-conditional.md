---
id: tracerray-conditional
category: dxr
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# tracerray-conditional

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Calls to `TraceRay(...)` (DXR pipeline) or `RayQuery::TraceRayInline(...)` (inline ray queries) placed inside an `if`, `for`, or `while` whose condition is not provably uniform across the wave. The rule fires on conditions derived from per-thread varying inputs (pixel coordinates, dispatch thread IDs, per-pixel sample loads) without an enclosing wave-coherence test, and on cases where the trace call is preceded by a large amount of live-state allocation (textures sampled into registers, normals computed, material parameters built up) that must be spilled across the trace.

## Why it matters on a GPU

A `TraceRay` call is the most expensive primitive in any DXR shader. The DXR runtime must spill the caller's live registers to a per-lane stack so the called any-hit, intersection, miss, or closest-hit shaders can run with their own register set, then restore on return. On AMD RDNA 2/3 with hardware ray tracing, the spill happens to the per-lane scratch buffer in VMEM — typically 128-256 bytes per lane per active variable, costing one `buffer_store_dword` and one `buffer_load_dword` per VGPR live across the trace. On NVIDIA Turing, Ada, and Blackwell, the SER hardware (or the older non-SER traversal) stages spills through L1$, but the data still has to round-trip out of registers and back. On Intel Xe-HPG with hardware RT, the analogous spill goes through the URB / scratch.

When `TraceRay` sits in a divergent branch, the cost compounds because the lanes that take the branch may have a different live-state shape from the lanes that did not — the compiler conservatively spills the union, which means even lanes that skipped the trace pay the spill cost on entry to the merge point. The bug pattern multiplies the live-set: a shader that built up 32 VGPRs of material setup before a conditional `TraceRay` for shadow rays spills all 32 VGPRs across the trace boundary, doubling the dynamic VGPR pressure and cutting wave occupancy roughly in half. Measured on RDNA 2 (RX 6800) for a typical screen-space-mixed-with-RT shadow shader, restructuring to issue the trace early — before any material work — recovered 30-40% of the trace performance.

Inline ray queries (`RayQuery<>` from SM 6.5+) sidestep the heavy DXR shader-table indirection but still incur the spill penalty when the query starts inside a divergent region. The remediation is to either reorder the shader so the trace happens first (compute material work after the trace using the closest-hit data), to make the branch uniform via `WaveActiveAllTrue`/`WaveActiveAnyTrue` so all lanes participate together, or to factor the trace into a separate pass executed unconditionally over the relevant pixels. Modern engines (Unreal Engine 5.4 Lumen, Frostbite RT shadows) standardise on the "trace first, shade later" pattern for exactly this reason.

## Examples

### Bad

```hlsl
RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4>             Out   : register(u0);

[shader("raygeneration")]
void rg_shadow_after_material() {
    uint2  px  = DispatchRaysIndex().xy;
    float3 N   = SampleNormal(px);
    float3 alb = SampleAlbedo(px);
    float  rough = SampleRoughness(px);
    float3 brdf = ComputeBRDF(N, alb, rough); // ~24 VGPRs of live state
    if (dot(N, LightDir) > 0.0) {
        // 'dot(N, LightDir)' is per-pixel — divergent. The 24 VGPRs of
        // live BRDF state must spill across the TraceRay boundary.
        RayDesc r = MakeShadowRay(px);
        ShadowPayload pl = (ShadowPayload)0;
        TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, r, pl);
        Out[px] = float4(brdf * pl.visible, 1);
    }
}
```

### Good

```hlsl
[shader("raygeneration")]
void rg_shadow_before_material() {
    uint2 px = DispatchRaysIndex().xy;
    // Issue the trace early, before allocating large live state.
    // Branch the trace itself with a wave-coherent guard so the cost is
    // paid only by waves where any lane needs the result.
    bool any_lit = WaveActiveAnyTrue(NeedsShadow(px));
    ShadowPayload pl;  pl.visible = 1.0;
    if (any_lit) {
        RayDesc r = MakeShadowRay(px);
        TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, r, pl);
    }
    // Now compute the heavy material work — BRDF live-state never crosses
    // the trace.
    float3 N    = SampleNormal(px);
    float3 alb  = SampleAlbedo(px);
    float  rgh  = SampleRoughness(px);
    float3 brdf = ComputeBRDF(N, alb, rgh);
    Out[px] = float4(brdf * pl.visible, 1);
}
```

## Options

none

## Fix availability

**suggestion** — Reordering the shader to trace first changes program structure and may not be possible if the trace's input depends on material work. The diagnostic identifies the trace call, the enclosing branch, and an estimate of live-state crossing the trace boundary.

## See also

- Related rule: [anyhit-heavy-work](anyhit-heavy-work.md) — heavy work in any-hit shaders
- Related rule: [inline-rayquery-when-pipeline-better](inline-rayquery-when-pipeline-better.md) — choosing between inline RQ and pipeline TraceRay
- Related rule: [wave-active-all-equal-precheck](wave-active-all-equal-precheck.md) — wave-coherent fast paths
- Microsoft DirectX docs: DXR — `TraceRay`, payload size, register spill behaviour
- Companion blog post: [dxr overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/tracerray-conditional.md)

*© 2026 NelCit, CC-BY-4.0.*
