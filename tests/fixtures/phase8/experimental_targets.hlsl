// Phase 8 — v0.10 IHV-experimental + DEFERRED fixture. These rules are gated
// behind `[experimental.target = ...]`; HIT annotations only meaningful when
// the matching experimental target is selected in the lint config.

// HIT(wave64-on-rdna4-compute-misses-dynamic-vgpr): WaveSize(64) under Rdna4.
[WaveSize(64)]
[numthreads(64, 1, 1)]
void cs_wave64() {}

// HIT(coopvec-fp4-fp6-blackwell-layout): FP4 with non-optimal layout under Blackwell.
void cs_fp4_bad() {
    MatrixVectorMul(COMPONENT_TYPE_FLOAT_E2M1, MATRIX_LAYOUT_ROW_MAJOR);
}

// HIT(wavesize-32-on-xe2-misses-simd16): WaveSize(32) under Xe2.
[WaveSize(32)]
[numthreads(32, 1, 1)]
void cs_xe2_ws32() {}

// HIT(cluster-id-without-cluster-geometry-feature-check): SM 6.10 ClusterID without guard.
[shader("closesthit")]
void ch_cluster(inout Payload p, in Attribs a) {
    uint id = ClusterID();
    p.x = id;
}

// HIT(oriented-bbox-not-set-on-rdna4): RT call under Rdna4.
[shader("raygeneration")]
void rg_obbox() {
    RaytracingAccelerationStructure scene;
    RayDesc r;
    Payload p;
    TraceRay(scene, 0, 0xFF, 0, 1, 0, r, p);
}

// HIT(numwaves-anchored-cap): total > 1024.
[numthreads(64, 32, 1)]
void cs_too_many() {}

// HIT(reference-data-type-not-supported-pre-sm610): `inout ref` on SM <= 6.9.
void f_ref(inout ref float v) { v = 0; }

// HIT(rga-pressure-bridge-stub): once-per-source informational note under Rdna4.
[numthreads(64, 1, 1)]
void cs_anything() {}
