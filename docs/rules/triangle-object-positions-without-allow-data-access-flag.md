---
id: triangle-object-positions-without-allow-data-access-flag
category: dxr
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# triangle-object-positions-without-allow-data-access-flag

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Every call site of `TriangleObjectPositions()` (SM 6.10 ray-tracing intrinsic).
Project-side state (the BLAS-build flag) is invisible from shader source, so
the rule fires on every call site as a reminder.

## Why it matters on a GPU

`TriangleObjectPositions()` requires the underlying acceleration structure to
have been built with `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX`
(D3D12) or `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR`
(Vulkan). Without the flag, the call is undefined behaviour: drivers may
return garbage positions, segfault, or silently fall back to an older BVH
layout.

## Examples

### Bad

```hlsl
[shader("closesthit")]
void ch(inout Payload p, in Attribs a) {
    float3 v[3] = TriangleObjectPositions(); // verify BLAS flag
}
```

### Good

CPU-side: ensure the BLAS build sets the
`USE_ORIENTED_BOUNDING_BOX` / `ALLOW_DATA_ACCESS` flag. Once verified, this
rule's diagnostic can be suppressed inline.

## Options

none

## Fix availability

**none** — Project-side configuration cannot be inspected from shader
source.

## See also

- Vulkan: `VK_KHR_ray_tracing_position_fetch`
- DirectX SM 6.10 preview blog
