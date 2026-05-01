---
id: coopvec-transpose-without-feature-check
category: cooperative-vector
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# coopvec-transpose-without-feature-check

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A cooperative-vector matrix-multiply call whose `transposed` flag is true (`MATRIX_FLAG_TRANSPOSED` set) without a corresponding application-side feature check on `D3D12_FEATURE_DATA_D3D12_OPTIONS18.CooperativeVectorTier`. The SM 6.9 cooperative-vector spec marks the transpose path as conditionally supported: tier 1 implementations may not honour it, and a runtime call with transpose set on an unsupporting device fails. The rule surfaces every transpose use so the developer can confirm the tier check is in place.

## Why it matters on a GPU

The transpose path lets the application swap the matrix's effective row/column orientation at the matmul site without re-uploading the data. On NVIDIA Ada Lovelace, the tensor cores expose hardware transpose as a free-rate option in the inference path; on AMD RDNA 3/4 WMMA, transpose support is gated behind a driver feature flag because the WMMA hardware lacks the on-the-fly swizzle and the driver emulates it via a software path; on Intel Xe-HPG XMX engines, transpose is supported but the cost varies by component type. The runtime exposes all of this through a single tier value: if the device is tier 0 or below the per-IHV transpose tier threshold, the transpose call fails.

The failure mode is a runtime device-removed event — the DXR-style "your dispatch crashed the device" error — not a clean rejection. Surfacing the transpose use at lint time gives the developer a chance to add the feature check, supply a fallback path that uploads a transposed copy of the matrix, or document the minimum-tier requirement in the project's README.

The rule is suggestion-tier because legitimate transpose use is part of the cooperative-vector design surface; the linter cannot tell whether the application has the feature check elsewhere. The diagnostic includes a one-line snippet of the recommended `CheckFeatureSupport` call.

## Examples

### Bad

```hlsl
// Transpose without an application-side tier check — fails on tier-0 devices.
ByteAddressBuffer g_Weights : register(t0);

[numthreads(32, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    using namespace dx::linalg;
    vector<float, 16> input  = LoadInput(tid);
    vector<float, 16> output;
    MatrixVectorMul(output, input,
                    g_Weights, 0, 64,
                    MATRIX_LAYOUT_INFERENCING_OPTIMAL,
                    MATRIX_FLAG_TRANSPOSED);     // requires tier check
}
```

### Good

```hlsl
// Application-side, before dispatch:
//   D3D12_FEATURE_DATA_D3D12_OPTIONS18 opts = {};
//   device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS18, &opts, sizeof(opts));
//   if (opts.CooperativeVectorTier >= D3D12_COOPERATIVE_VECTOR_TIER_1_1) { ... }
//
// Shader path with transpose is unchanged; an upload-time fallback copies
// the matrix transposed when the tier is below the threshold.
```

## Options

none

## Fix availability

**suggestion** — The fix is an application-side feature check + a fallback upload path. The diagnostic names the call site and emits the recommended `CheckFeatureSupport` snippet as a comment.

## See also

- Related rule: [coopvec-non-optimal-matrix-layout](coopvec-non-optimal-matrix-layout.md) — companion perf rule
- Related rule: [coopvec-non-uniform-matrix-handle](coopvec-non-uniform-matrix-handle.md) — uniformity sibling rule
- HLSL specification: [proposal 0029 Cooperative Vector](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/coopvec-transpose-without-feature-check.md)

*© 2026 NelCit, CC-BY-4.0.*
