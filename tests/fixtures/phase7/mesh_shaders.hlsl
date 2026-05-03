// Phase 7 — mesh / amplification shader rules (SM 6.5+).
// Hand-written fixture for shader-clippy rule validation.
// Convention: `// HIT(rule-id): reason` on offending lines.
// `// SHOULD-NOT-HIT(rule-id): reason` on near-miss lines that must NOT fire.

struct MeshVertex {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct MeshPrimitive {
    uint  matIndex : TEXCOORD1;
};

// --- mesh-numthreads-over-128 ---

// HIT(mesh-numthreads-over-128): 16*12*1 = 192 threads — exceeds the 128-thread
// limit for mesh shaders; PSO creation will fail at runtime.
[numthreads(16, 12, 1)]
[outputtopology("triangle")]
void ms_too_many_threads(
        uint gi : SV_GroupIndex,
        out vertices MeshVertex verts[64],
        out indices  uint3      prims[64],
        out primitives MeshPrimitive primData[64]) {
    SetMeshOutputCounts(64, 64);
    if (gi < 64u) {
        verts[gi].position = float4(0, 0, 0, 1);
        verts[gi].normal   = float3(0, 1, 0);
        verts[gi].uv       = float2(0, 0);
    }
    if (gi < 64u) {
        prims[gi]          = uint3(gi * 3, gi * 3 + 1, gi * 3 + 2);
        primData[gi].matIndex = 0;
    }
}

// SHOULD-NOT-HIT(mesh-numthreads-over-128): 8*8*1 = 64 threads — within limit.
[numthreads(8, 8, 1)]
[outputtopology("triangle")]
void ms_good_threads(
        uint gi : SV_GroupIndex,
        out vertices MeshVertex verts[64],
        out indices  uint3      prims[64],
        out primitives MeshPrimitive primData[64]) {
    SetMeshOutputCounts(64, 64);
    if (gi < 64u) {
        verts[gi].position = float4(0, 0, 0, 1);
        verts[gi].normal   = float3(0, 1, 0);
        verts[gi].uv       = float2(0, 0);
    }
    if (gi < 64u) {
        prims[gi]          = uint3(gi * 3, gi * 3 + 1, gi * 3 + 2);
        primData[gi].matIndex = 0;
    }
}

// --- mesh-output-decl-exceeds-256 ---

// HIT(mesh-output-decl-exceeds-256): out vertices count 300 > 256 — invalid for
// HIT(output-count-overrun): SetMeshOutputCounts(300, 256) -- 300 > the 300-vertex
// declared ceiling (well above 256-cap); the Phase 7 rule (ADR 0017) flags the
// literal-count path even when the array sized at 300 just barely matches.
// the mesh shader spec; PSO creation fails.
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void ms_too_many_verts(
        uint gi : SV_GroupIndex,
        out vertices MeshVertex verts[300],
        out indices  uint3      prims[256],
        out primitives MeshPrimitive primData[256]) {
    SetMeshOutputCounts(300, 256);
    if (gi < 128u) {
        verts[gi].position = float4((float)gi, 0, 0, 1);
        verts[gi].normal   = float3(0, 1, 0);
        verts[gi].uv       = float2(0, 0);
        prims[gi]          = uint3(0, 1, 2);
        primData[gi].matIndex = gi;
    }
}

// --- meshlet-vertex-count-bad ---

// HIT(meshlet-vertex-count-bad): meshlet declares only 3 vertices for 64 primitives;
// a triangle mesh needs at least N+2 vertices for N triangles in a strip, or 3*N
// for indexed quads/fans — this count is almost certainly wrong.
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void ms_bad_vertex_count(
        uint gi : SV_GroupIndex,
        out vertices MeshVertex verts[3],
        out indices  uint3      prims[64],
        out primitives MeshPrimitive primData[64]) {
    SetMeshOutputCounts(3, 64);
    if (gi < 3u) {
        verts[gi].position = float4((float)gi, 0, 0, 1);
        verts[gi].normal   = float3(0, 1, 0);
        verts[gi].uv       = float2(0, 0);
    }
    if (gi < 64u) {
        prims[gi] = uint3(0, 1, 2);
        primData[gi].matIndex = gi;
    }
}

// --- setmeshoutputcounts-in-divergent-cf ---

// HIT(setmeshoutputcounts-in-divergent-cf): SetMeshOutputCounts is inside a
// per-thread branch — only some threads call it; spec requires all threads
// (or exactly one designated thread) to call it uniformly.
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void ms_divergent_set_counts(
        uint gi : SV_GroupIndex,
        out vertices MeshVertex verts[64],
        out indices  uint3      prims[64],
        out primitives MeshPrimitive primData[64]) {
    if (gi == 0u) {
        SetMeshOutputCounts(64, 64);  // correct: uniform lane
    }
    if (gi < 32u) {
        // HIT(setmeshoutputcounts-in-divergent-cf): second call inside a divergent
        // branch — calling SetMeshOutputCounts more than once is also UB.
        SetMeshOutputCounts(32, 32);
    }
    if (gi < 64u) {
        verts[gi].position = float4(0, 0, 0, 1);
        verts[gi].normal   = float3(0, 1, 0);
        verts[gi].uv       = float2(0, 0);
        prims[gi]          = uint3(0, 1, 2);
        primData[gi].matIndex = gi;
    }
}

// --- as-payload-over-16k ---

// HIT(as-payload-over-16k): payload struct is 16640 bytes — exceeds the 16384-byte
// amplification-shader payload limit; Slang reflection exposes the layout size.
struct HugeASPayload {
    float4 data[1040];   // 1040 * 16 = 16640 bytes
};

[numthreads(32, 1, 1)]
void as_oversized_payload(uint gi : SV_GroupIndex) {
    HugeASPayload payload = (HugeASPayload)0;
    payload.data[gi] = float4(1, 0, 0, 1);
    DispatchMesh(1, 1, 1, payload);
}

// Pixel shader stub.
float4 ps_mesh(MeshVertex pin) : SV_Target {
    return float4(pin.normal * 0.5 + 0.5, 1.0);
}
