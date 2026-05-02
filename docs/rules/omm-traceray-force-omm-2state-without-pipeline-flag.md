---
id: omm-traceray-force-omm-2state-without-pipeline-flag
category: opacity-micromaps
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# omm-traceray-force-omm-2state-without-pipeline-flag

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `TraceRay(...)` call with `RAY_FLAG_FORCE_OMM_2_STATE` set when the DXR pipeline subobject's `D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS` is not set. The rule reads the pipeline-flags subobject through Slang reflection and compares it against the constant-folded ray flags at the trace site; mismatch fires.

## Why it matters on a GPU

The DXR 1.2 OMM specification has a project-level gate (`D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS`) and a per-trace gate (the ray flag of the same name). Both must be set for OMM consultation to happen on a given trace. The pipeline flag is set in the state-object's pipeline-config subobject and reflects the application's promise to the runtime that OMM blocks may be present in the BVH; the per-trace flag is the request.

`RAY_FLAG_FORCE_OMM_2_STATE` is the per-trace setting that forces OMM's four-state output to two states (opaque vs. transparent). It is meaningful only when OMM is consulted, which requires both gates. When the per-trace flag is set but the pipeline flag is missing, the runtime treats the trace as undefined behaviour — the DXC validator catches the simplest forms; the lint catches the rest by reading the pipeline subobject from reflection.

The pipeline flag is set on the application side, in the `D3D12_RAYTRACING_PIPELINE_CONFIG1` subobject that describes the state object. The lint diagnostic names the offending trace and the missing pipeline flag so the developer can patch the C++ side.

## Examples

### Bad

```hlsl
// Pipeline subobject (set application-side) does NOT include
// D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS.
// The trace below sets the per-trace force-2-state flag — UB.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    TraceRay(g_BVH,
             RAY_FLAG_FORCE_OMM_2_STATE | RAY_FLAG_ALLOW_OPACITY_MICROMAPS,
             0xFF, 0, 1, 0, MakeRay(), p); // ERROR: pipeline flag missing
}
```

### Good

```hlsl
// Application-side: state-object includes
//   D3D12_RAYTRACING_PIPELINE_CONFIG1{
//       MaxTraceRecursionDepth = 1,
//       Flags = D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS,
//   };
//
// Shader trace is now well-defined.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    TraceRay(g_BVH,
             RAY_FLAG_FORCE_OMM_2_STATE | RAY_FLAG_ALLOW_OPACITY_MICROMAPS,
             0xFF, 0, 1, 0, MakeRay(), p);
}
```

## Options

none

## Fix availability

**none** — The pipeline subobject is set on the application side. The diagnostic names the missing flag.

## See also

- Related rule: [omm-rayquery-force-2state-without-allow-flag](omm-rayquery-force-2state-without-allow-flag.md) — companion OMM rule for the inline-rayquery path
- Related rule: [omm-allocaterayquery2-non-const-flags](omm-allocaterayquery2-non-const-flags.md) — companion OMM rule
- HLSL specification: [proposal 0024 Opacity Micromaps](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0024-opacity-micromaps.md)
- D3D12 specification: `D3D12_RAYTRACING_PIPELINE_CONFIG1` flags
- Companion blog post: [opacity-micromaps overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/omm-traceray-force-omm-2state-without-pipeline-flag.md)

*© 2026 NelCit, CC-BY-4.0.*
