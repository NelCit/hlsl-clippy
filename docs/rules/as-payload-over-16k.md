---
id: as-payload-over-16k
category: mesh
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# as-payload-over-16k

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0007)*

## What it detects

The `payload` struct passed from an amplification shader (`DispatchMesh`) to its child mesh shaders whose total size exceeds the 16,384-byte (16 KB) D3D12 mesh-pipeline cap. The rule walks the struct definition reachable from the amplification entry's payload parameter, sums field sizes after HLSL packing rules, and fires when the total exceeds the cap. Slang reflection provides the per-field byte offsets directly.

## Why it matters on a GPU

The amplification-shader payload lives in a per-AS-group LDS-style region that the pipeline reserves at workgroup launch time. On NVIDIA Turing/Ada Lovelace and AMD RDNA 2/3, the AS payload is staged through on-chip memory between AS and the launched mesh-shader workgroups; on Intel Xe-HPG it occupies a per-AS slot in the pipeline scoreboard. The 16 KB cap is the contract every IHV ships, sized to fit comfortably in the on-chip staging buffer on RDNA 2 (the most LDS-constrained of the three).

Exceeding the cap is a hard PSO-creation failure (`D3D12CreateGraphicsPipelineState` returns `E_INVALIDARG`). The diagnostic surfaces the constraint at lint time so the author can decide whether to shrink the payload or refactor toward a side-buffer-indexed-by-`SV_DispatchThreadID` design.

Payloads that approach the cap also have a perf cost even when they fit: every byte of payload occupies LDS slot space that could be used by mesh-shader output buffers, which directly reduces wave occupancy on RDNA 2/3. Production AS payloads typically stay well under 1 KB — meshlet-id arrays, an LOD selector, and a culling decision are usually all that's needed.

## Examples

### Bad

```hlsl
// 64 KB payload — well over the 16 KB cap. PSO creation fails.
struct Payload {
    uint   meshletIDs[16384];  // 16 * 4 KB = 64 KB
};

groupshared Payload s_payload;

[shader("amplification")]
[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    s_payload.meshletIDs[tid] = ComputeMeshletID(tid);
    DispatchMesh(1, 1, 1, s_payload);
}
```

### Good

```hlsl
// 256-byte payload — well under the cap. The actual meshlet data lives in a
// StructuredBuffer indexed by SV_DispatchThreadID inside the mesh shader.
struct Payload {
    uint   visibleMeshletCount;
    uint   meshletIndexBase;
    uint   lodLevel;
    uint   _pad;
};

groupshared Payload s_payload;

[shader("amplification")]
[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    if (tid == 0) {
        s_payload.visibleMeshletCount = CountVisible();
        s_payload.meshletIndexBase    = MeshletBase();
        s_payload.lodLevel            = SelectLOD();
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(s_payload.visibleMeshletCount, 1, 1, s_payload);
}
```

## Options

none

## Fix availability

**none** — Shrinking the payload is a structural decision: which fields are needed by which child mesh shader, and which can be re-derived inside the mesh shader from `SV_DispatchThreadID`. The diagnostic names the over-shoot in bytes so the author can prioritise.

## See also

- Related rule: [oversized-ray-payload](oversized-ray-payload.md) — analogous cap on DXR ray payloads
- Related rule: [mesh-numthreads-over-128](mesh-numthreads-over-128.md) — companion mesh-pipeline cap
- Related rule: [mesh-output-decl-exceeds-256](mesh-output-decl-exceeds-256.md) — companion mesh-pipeline cap
- D3D12 specification: Amplification Shader payload size cap (16 KB)
- Companion blog post: [mesh overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/as-payload-over-16k.md)

*© 2026 NelCit, CC-BY-4.0.*
