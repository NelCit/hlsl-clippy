# Rules Catalog

> **Status:** pre-v0 — rule pages will populate as rules ship. The catalog below lists planned categories only. See [ROADMAP](../../ROADMAP.md).

All rules are grouped by category. Each rule page follows the [canonical template](_template.md).

## Severity legend

| Severity | Meaning |
|----------|---------|
| `error`  | Correctness issue or undefined behaviour. Blocks CI when mode is `deny`. |
| `warn`   | Performance anti-pattern or code smell. Default for most rules.          |
| `info`   | Style or modernisation hint. Never blocks CI.                            |

## Applicability legend

| Applicability        | Meaning |
|----------------------|---------|
| `machine-applicable` | The fix can be applied automatically and is always correct.              |
| `suggestion`         | A fix is shown but requires human intent-verification before applying.   |
| `none`               | No automated fix; the diagnostic only explains the problem.              |

## Categories

### math

Arithmetic simplifications that a compiler _might_ optimise but frequently does not across all targets.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `pow-to-mul` | warn | machine-applicable | 2 |
| `pow-base-two-to-exp2` | warn | machine-applicable | 2 |
| `pow-integer-decomposition` | warn | suggestion | 2 |
| `inv-sqrt-to-rsqrt` | warn | machine-applicable | 2 |
| `lerp-extremes` | warn | machine-applicable | 2 |
| `mul-identity` | warn | machine-applicable | 2 |
| `sin-cos-pair` | warn | machine-applicable | 2 |
| `manual-reflect` | warn | machine-applicable | 2 |
| `manual-refract` | warn | suggestion | 2 |
| `manual-distance` | warn | machine-applicable | 2 |
| `manual-step` | warn | machine-applicable | 2 |
| `manual-smoothstep` | warn | machine-applicable | 2 |
| `length-comparison` | warn | machine-applicable | 2 |

### saturate-redundancy

Redundant or double-saturate patterns and equivalent clamp forms.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `redundant-saturate` | warn | machine-applicable | 2 |
| `clamp01-to-saturate` | warn | machine-applicable | 2 |
| `redundant-normalize` | warn | machine-applicable | 2 |
| `redundant-transpose` | warn | machine-applicable | 2 |
| `redundant-abs` | warn | machine-applicable | 2 |

### bindings

cbuffer layout, resource binding, and register-space issues.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `non-uniform-resource-index` | warn | suggestion | 3 |
| `cbuffer-padding-hole` | warn | suggestion | 3 |
| `bool-straddles-16b` | error | suggestion | 3 |
| `oversized-cbuffer` | warn | none | 3 |
| `cbuffer-fits-rootconstants` | info | suggestion | 3 |
| `structured-buffer-stride-mismatch` | warn | suggestion | 3 |
| `unused-cbuffer-field` | warn | none | 3 |
| `dead-store-sv-target` | warn | none | 3 |
| `rwresource-read-only-usage` | warn | suggestion | 3 |

### texture

Sampling and texture access patterns.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `samplelevel-with-zero-on-mipped-tex` | warn | suggestion | 3 |
| `texture-as-buffer` | info | suggestion | 3 |
| `samplegrad-with-constant-grads` | warn | machine-applicable | 3 |
| `gather-channel-narrowing` | info | suggestion | 3 |
| `samplecmp-vs-manual-compare` | warn | suggestion | 3 |
| `texture-array-known-slice-uniform` | info | suggestion | 3 |

### workgroup

Compute shader `numthreads` and groupshared memory issues.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `numthreads-not-wave-aligned` | warn | suggestion | 3 |
| `numthreads-too-small` | warn | none | 3 |
| `groupshared-too-large` | warn | none | 3 |

### control-flow

Control flow divergence, barrier placement, and loop structure.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `loop-invariant-sample` | warn | suggestion | 4 |
| `cbuffer-load-in-loop` | warn | suggestion | 4 |
| `redundant-computation-in-branch` | warn | suggestion | 4 |
| `branch-on-uniform-missing-attribute` | info | suggestion | 4 |
| `small-loop-no-unroll` | info | suggestion | 4 |
| `discard-then-work` | warn | none | 4 |
| `groupshared-uninitialized-read` | error | none | 4 |

### interpolators

Vertex-to-pixel interpolator budget and qualifier mismatches.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `excess-interpolators` | warn | none | 3 |
| `nointerpolation-mismatch` | warn | suggestion | 3 |
| `missing-precise-on-pcf` | warn | suggestion | 3 |

### numerical-safety

Floating-point correctness hazards.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `acos-without-saturate` | warn | machine-applicable | 4 |
| `div-without-epsilon` | warn | none | 4 |
| `sqrt-of-potentially-negative` | warn | none | 4 |
| `compare-equal-float` | warn | none | 2 |
| `comparison-with-nan-literal` | error | none | 2 |
| `redundant-precision-cast` | warn | machine-applicable | 2 |

### ir-level

Rules that operate on compiled DXIL / SPIR-V. Phase 7, research-grade.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `vgpr-pressure-warning` | warn | none | 7 |
| `scratch-from-dynamic-indexing` | warn | none | 7 |
| `redundant-texture-sample` | warn | none | 7 |
| `min16float-opportunity` | info | suggestion | 7 |
| `unpack-then-repack` | warn | suggestion | 7 |
| `manual-f32tof16` | warn | machine-applicable | 7 |

### dxr

Ray tracing (DXR) payload and flag rules.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `oversized-ray-payload` | warn | none | 7 |
| `missing-accept-first-hit` | info | suggestion | 7 |
| `recursion-depth-not-declared` | error | none | 7 |

### mesh

Mesh and amplification shader issues.

| Rule | Default severity | Applicability | Phase |
|------|-----------------|---------------|-------|
| `meshlet-vertex-count-bad` | warn | none | 7 |
| `output-count-overrun` | error | none | 7 |

### work-graphs

Work graph shader rules (planned, post-1.0).

### vrs

Variable-rate shading rules (planned, post-1.0).

### sampler-feedback

Sampler feedback rules (planned, post-1.0).

### fp16-packed

Half-precision and packed-data rules (planned, post-1.0).

---

For contributors adding a new rule: start from [docs/rules/_template.md](_template.md).
