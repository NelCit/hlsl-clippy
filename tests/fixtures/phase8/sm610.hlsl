// Phase 8 — v0.8 SM 6.10 + stub-burndown fixture.
// Conventions: `// HIT(rule-id): reason` and `// SHOULD-NOT-HIT(rule-id): reason`.

#include <linalg.h>

// HIT(linalg-matrix-non-optimal-layout): row-major layout on linalg matmul.
// HIT(linalg-matrix-element-type-mismatch): fp16 input, fp32 accumulator.
void cs_linalg_bad() {
    linalg::MatrixVectorMul(COMPONENT_TYPE_FLOAT16, COMPONENT_TYPE_FLOAT32, MATRIX_LAYOUT_ROW_MAJOR);
}

// SHOULD-NOT-HIT(linalg-matrix-non-optimal-layout): optimal layout used.
void cs_linalg_ok() {
    linalg::MatrixVectorMul(COMPONENT_TYPE_FLOAT16, COMPONENT_TYPE_FLOAT16, MATRIX_LAYOUT_INFERENCING_OPTIMAL);
}

// HIT(getgroupwaveindex-without-wavesize-attribute): GetGroupWaveIndex without [WaveSize].
[numthreads(64, 1, 1)]
void cs_no_wavesize(uint3 dt : SV_DispatchThreadID) {
    uint i = GetGroupWaveIndex();
    (void)i;
}

// SHOULD-NOT-HIT(getgroupwaveindex-without-wavesize-attribute): WaveSize is pinned.
[WaveSize(32)]
[numthreads(64, 1, 1)]
void cs_with_wavesize(uint3 dt : SV_DispatchThreadID) {
    uint i = GetGroupWaveIndex();
    (void)i;
}

// HIT(groupshared-over-32k-without-attribute): 36 KB groupshared, no attribute.
groupshared float lds_big[9000];
[numthreads(64, 1, 1)]
void cs_big_lds() {
    lds_big[0] = 0.0f;
}

// HIT(triangle-object-positions-without-allow-data-access-flag): SM 6.10 RT call.
[shader("closesthit")]
void ch_pos(inout Payload p, in Attribs a) {
    float3 v[3] = TriangleObjectPositions();
    p.color = v[0];
}

// HIT(numthreads-not-wave-aligned): 17 is not a multiple of 32.
[numthreads(17, 1, 1)]
void cs_unaligned(uint3 dt : SV_DispatchThreadID) { (void)dt; }

// SHOULD-NOT-HIT(numthreads-not-wave-aligned): 64 is wave-aligned.
[numthreads(8, 8, 1)]
void cs_aligned(uint3 dt : SV_DispatchThreadID) { (void)dt; }

// HIT(dispatchmesh-grid-too-small-for-wave): grid total 4 < wave 32.
[numthreads(1, 1, 1)]
[shader("amplification")]
void as_small_grid(uint3 dt : SV_DispatchThreadID) {
    DispatchMesh(4, 1, 1);
}

// HIT(dot4add-opportunity): 4-component dot expansion.
float dot4_bad(float4 a, float4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// SHOULD-NOT-HIT(dot4add-opportunity): only 3 components.
float dot3_ok(float4 a, float4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
