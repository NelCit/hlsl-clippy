// Phase 8 -- v0.10 IHV-experimental + DEFERRED fixture. These rules are gated
// behind `[experimental.target = ...]`; HIT annotations only meaningful when
// the matching experimental target is selected in the lint config.
//
// The fixture is consumed by `tests/unit/test_ihv_target_snapshot.cpp` and
// by the CI `ihv-target-snapshot` job. Both invoke the linter four times --
// once per `ExperimentalTarget` value -- and check that:
//
//   * default config (`ExperimentalTarget::None`) emits zero IHV-target-
//     gated diagnostics on this file (criterion #11 of ADR 0018 §5);
//   * each non-`None` target surfaces at least one diagnostic whose code
//     contains the matching IHV slug (`rdna4` / `blackwell` / `xe2`).
//
// The file is kept syntactically valid for Slang reflection in the default
// `sm_6_6` profile so that the reflection-stage rules
// (`wave64-on-rdna4-...` and `wavesize-32-on-xe2-...`) actually run. SM 6.10
// preview surfaces (`linalg::Matrix`, `ClusterID()`, `inout ref` parameters)
// would break Slang reflection in `sm_6_6`, so they are kept out of this
// fixture; the AST-only rules they target have their own dedicated unit
// tests under `tests/unit/test_<rule_id>.cpp`.

// HIT(wave64-on-rdna4-compute-misses-dynamic-vgpr): WaveSize(64) under Rdna4.
[shader("compute")]
[WaveSize(64)]
[numthreads(64, 1, 1)]
void cs_wave64() {}

// HIT(wavesize-32-on-xe2-misses-simd16): WaveSize(32) under Xe2.
[shader("compute")]
[WaveSize(32)]
[numthreads(32, 1, 1)]
void cs_xe2_ws32() {}

// HIT(coopvec-fp4-fp6-blackwell-layout): FP4 with non-optimal layout under
// Blackwell. The rule is AST-stage and matches the `MatrixVectorMul(...)`
// + `MATRIX_LAYOUT_ROW_MAJOR` token shape via tree-sitter; the identifiers
// don't need to resolve in the Slang stdlib for the AST match to fire.
//
// We mention the textual pattern inside a string-literal-style identifier
// so tree-sitter still parses it as a call expression but Slang's
// reflection step does not chase the symbol -- macro-style identifiers in
// dead code paths flow through specialisation as opaque references.
//
// The simpler way to spell that: declare a stub function with the same
// name, and resolve the layout enum to a real integer. The rule's text
// matcher only inspects the literal arg names against its allow-list.
#define COMPONENT_TYPE_FLOAT_E2M1 0
#define MATRIX_LAYOUT_ROW_MAJOR 0
void MatrixVectorMul(int component_type, int layout) {}

[shader("compute")]
[numthreads(32, 1, 1)]
void cs_fp4_bad() {
    MatrixVectorMul(COMPONENT_TYPE_FLOAT_E2M1, MATRIX_LAYOUT_ROW_MAJOR);
}

// HIT(numwaves-anchored-cap): total > 1024.
[shader("compute")]
[numthreads(64, 32, 1)]
void cs_too_many() {}

// HIT(rga-pressure-bridge-stub): once-per-source informational note under Rdna4.
[shader("compute")]
[numthreads(64, 1, 1)]
void cs_anything() {}
