---
title: "hlsl-clippy blog"
layout: doc
---

# hlsl-clippy blog

One post per rule. Each post explains the GPU mechanism behind a lint warning
in enough depth that you could derive the rule yourself.

The target reader is a graphics engineer who ships shaders to production and has
not profiled them in a while. Posts assume familiarity with HLSL and shader
stages.

## v0.5.0 launch series — category overviews

The v0.5.0 launch shipped eight category overviews plus a preface essay.
Each overview walks one rule-pack at GPU-mechanism level.

| Date | Category | Title |
|------|----------|-------|
| 2026-05-01 | _preface_ | [Why your HLSL is slower than it has to be](./why-your-hlsl-is-slower-than-it-has-to-be.md) |
| 2026-05-01 | math | [Where the cycles go: math intrinsics on modern GPUs](./math-overview.md) |
| 2026-05-01 | workgroup | [Your groupshared array is bank-conflicting on RDNA](./workgroup-overview.md) |
| 2026-05-01 | control-flow | [Divergent control flow is the silent killer of your shader](./control-flow-overview.md) |
| 2026-05-01 | bindings | [Where root signatures and descriptor heaps quietly cost you](./bindings-overview.md) |
| 2026-05-01 | texture | [Texture sampling is doing more work than your shader admits](./texture-overview.md) |
| 2026-05-01 | mesh + dxr | [Mesh shaders + DXR](./mesh-dxr-overview.md) |
| 2026-05-01 | wave + helper-lane | [Wave intrinsics and helper-lane traps](./wave-helper-lane-overview.md) |
| 2026-05-01 | sm 6.9 | [SM 6.9: shader execution reordering, cooperative vectors, and the new ray-tracing primitives](./ser-coop-vector-overview.md) |

## Per-rule deep dives

Per-rule posts (one per shipped rule) are grouped by category below. Each
entry links to a stub or full post. Stubs are placeholder scaffolds queued
for v1.0.x patch-release fill-in per
[ADR 0018 §5 criterion #6](../decisions/0018-v08-research-direction.md).

<details>
<summary><strong>bindings</strong> (26 rules)</summary>

- [`all-resources-bound-not-set`](./all-resources-bound-not-set.md) _(stub)_
- [`bool-straddles-16b`](./bool-straddles-16b.md) _(stub)_
- [`buffer-load-width-vs-cache-line`](./buffer-load-width-vs-cache-line.md) _(stub)_
- [`byteaddressbuffer-load-misaligned`](./byteaddressbuffer-load-misaligned.md) _(stub)_
- [`byteaddressbuffer-narrow-when-typed-fits`](./byteaddressbuffer-narrow-when-typed-fits.md) _(stub)_
- [`cbuffer-divergent-index`](./cbuffer-divergent-index.md) _(stub)_
- [`cbuffer-fits-rootconstants`](./cbuffer-fits-rootconstants.md) _(stub)_
- [`cbuffer-large-fits-rootcbv-not-table`](./cbuffer-large-fits-rootcbv-not-table.md) _(stub)_
- [`cbuffer-padding-hole`](./cbuffer-padding-hole.md) _(stub)_
- [`dead-store-sv-target`](./dead-store-sv-target.md) _(stub)_
- [`descriptor-heap-no-non-uniform-marker`](./descriptor-heap-no-non-uniform-marker.md) _(stub)_
- [`descriptor-heap-type-confusion`](./descriptor-heap-type-confusion.md) _(stub)_
- [`divergent-buffer-index-on-uniform-resource`](./divergent-buffer-index-on-uniform-resource.md) _(stub)_
- [`excess-interpolators`](./excess-interpolators.md) _(stub)_
- [`missing-precise-on-pcf`](./missing-precise-on-pcf.md) _(stub)_
- [`nointerpolation-mismatch`](./nointerpolation-mismatch.md) _(stub)_
- [`non-uniform-resource-index`](./non-uniform-resource-index.md) _(stub)_
- [`oversized-cbuffer`](./oversized-cbuffer.md) _(stub)_
- [`rov-without-earlydepthstencil`](./rov-without-earlydepthstencil.md) _(stub)_
- [`rwbuffer-store-without-globallycoherent`](./rwbuffer-store-without-globallycoherent.md) _(stub)_
- [`rwresource-read-only-usage`](./rwresource-read-only-usage.md) _(stub)_
- [`static-sampler-when-dynamic-used`](./static-sampler-when-dynamic-used.md) _(stub)_
- [`structured-buffer-stride-mismatch`](./structured-buffer-stride-mismatch.md) _(stub)_
- [`structured-buffer-stride-not-cache-aligned`](./structured-buffer-stride-not-cache-aligned.md) _(stub)_
- [`uav-srv-implicit-transition-assumed`](./uav-srv-implicit-transition-assumed.md) _(stub)_
- [`unused-cbuffer-field`](./unused-cbuffer-field.md) _(stub)_

</details>

<details>
<summary><strong>blackwell</strong> (1 rule)</summary>

- [`coopvec-fp4-fp6-blackwell-layout`](./coopvec-fp4-fp6-blackwell-layout.md) _(stub)_

</details>

<details>
<summary><strong>control-flow</strong> (21 rules)</summary>

- [`barrier-in-divergent-cf`](./barrier-in-divergent-cf.md) _(stub)_
- [`branch-on-uniform-missing-attribute`](./branch-on-uniform-missing-attribute.md) _(stub)_
- [`cbuffer-load-in-loop`](./cbuffer-load-in-loop.md) _(stub)_
- [`clip-from-non-uniform-cf`](./clip-from-non-uniform-cf.md) _(stub)_
- [`derivative-in-divergent-cf`](./derivative-in-divergent-cf.md) _(stub)_
- [`discard-then-work`](./discard-then-work.md) _(stub)_
- [`early-z-disabled-by-conditional-discard`](./early-z-disabled-by-conditional-discard.md) _(stub)_
- [`flatten-on-uniform-branch`](./flatten-on-uniform-branch.md) _(stub)_
- [`forcecase-missing-on-ps-switch`](./forcecase-missing-on-ps-switch.md) _(stub)_
- [`groupshared-uninitialized-read`](./groupshared-uninitialized-read.md) _(stub)_
- [`loop-attribute-conflict`](./loop-attribute-conflict.md) _(stub)_
- [`loop-invariant-sample`](./loop-invariant-sample.md) _(stub)_
- [`manual-wave-reduction-pattern`](./manual-wave-reduction-pattern.md) _(stub)_
- [`quadany-quadall-opportunity`](./quadany-quadall-opportunity.md) _(stub)_
- [`redundant-computation-in-branch`](./redundant-computation-in-branch.md) _(stub)_
- [`sample-in-loop-implicit-grad`](./sample-in-loop-implicit-grad.md) _(stub)_
- [`small-loop-no-unroll`](./small-loop-no-unroll.md) _(stub)_
- [`wave-active-all-equal-precheck`](./wave-active-all-equal-precheck.md) _(stub)_
- [`wave-intrinsic-helper-lane-hazard`](./wave-intrinsic-helper-lane-hazard.md) _(stub)_
- [`wave-intrinsic-non-uniform`](./wave-intrinsic-non-uniform.md) _(stub)_
- [`wavereadlaneat-constant-non-zero-portability`](./wavereadlaneat-constant-non-zero-portability.md) _(stub)_

</details>

<details>
<summary><strong>cooperative-vector</strong> (6 rules)</summary>

- [`coopvec-base-offset-misaligned`](./coopvec-base-offset-misaligned.md) _(stub)_
- [`coopvec-fp8-with-non-optimal-layout`](./coopvec-fp8-with-non-optimal-layout.md) _(stub)_
- [`coopvec-non-optimal-matrix-layout`](./coopvec-non-optimal-matrix-layout.md) _(stub)_
- [`coopvec-non-uniform-matrix-handle`](./coopvec-non-uniform-matrix-handle.md) _(stub)_
- [`coopvec-stride-mismatch`](./coopvec-stride-mismatch.md) _(stub)_
- [`coopvec-transpose-without-feature-check`](./coopvec-transpose-without-feature-check.md) _(stub)_

</details>

<details>
<summary><strong>dxr</strong> (10 rules)</summary>

- [`anyhit-heavy-work`](./anyhit-heavy-work.md) _(stub)_
- [`inline-rayquery-when-pipeline-better`](./inline-rayquery-when-pipeline-better.md) _(stub)_
- [`missing-accept-first-hit`](./missing-accept-first-hit.md) _(stub)_
- [`missing-ray-flag-cull-non-opaque`](./missing-ray-flag-cull-non-opaque.md) _(stub)_
- [`oversized-ray-payload`](./oversized-ray-payload.md) _(stub)_
- [`pipeline-when-inline-better`](./pipeline-when-inline-better.md) _(stub)_
- [`ray-flag-force-opaque-with-anyhit`](./ray-flag-force-opaque-with-anyhit.md) _(stub)_
- [`recursion-depth-not-declared`](./recursion-depth-not-declared.md) _(stub)_
- [`tracerray-conditional`](./tracerray-conditional.md) _(stub)_
- [`triangle-object-positions-without-allow-data-access-flag`](./triangle-object-positions-without-allow-data-access-flag.md) _(stub)_

</details>

<details>
<summary><strong>linalg</strong> (2 rules)</summary>

- [`linalg-matrix-element-type-mismatch`](./linalg-matrix-element-type-mismatch.md) _(stub)_
- [`linalg-matrix-non-optimal-layout`](./linalg-matrix-non-optimal-layout.md) _(stub)_

</details>

<details>
<summary><strong>long-vectors</strong> (4 rules)</summary>

- [`long-vector-bytebuf-load-misaligned`](./long-vector-bytebuf-load-misaligned.md) _(stub)_
- [`long-vector-in-cbuffer-or-signature`](./long-vector-in-cbuffer-or-signature.md) _(stub)_
- [`long-vector-non-elementwise-intrinsic`](./long-vector-non-elementwise-intrinsic.md) _(stub)_
- [`long-vector-typed-buffer-load`](./long-vector-typed-buffer-load.md) _(stub)_

</details>

<details>
<summary><strong>math</strong> (31 rules)</summary>

- [`acos-without-saturate`](./acos-without-saturate.md) _(stub)_
- [`countbits-vs-manual-popcount`](./countbits-vs-manual-popcount.md) _(stub)_
- [`cross-with-up-vector`](./cross-with-up-vector.md) _(stub)_
- [`div-without-epsilon`](./div-without-epsilon.md) _(stub)_
- [`dot4add-opportunity`](./dot4add-opportunity.md) _(stub)_
- [`dot-on-axis-aligned-vector`](./dot-on-axis-aligned-vector.md) _(stub)_
- [`firstbit-vs-log2-trick`](./firstbit-vs-log2-trick.md) _(stub)_
- [`inv-sqrt-to-rsqrt`](./inv-sqrt-to-rsqrt.md) _(stub)_
- [`isnormal-pre-sm69`](./isnormal-pre-sm69.md) _(stub)_
- [`isspecialfloat-implicit-fp16-promotion`](./isspecialfloat-implicit-fp16-promotion.md) _(stub)_
- [`length-comparison`](./length-comparison.md) _(stub)_
- [`length-then-divide`](./length-then-divide.md) _(stub)_
- [`lerp-extremes`](./lerp-extremes.md) _(stub)_
- [`lerp-on-bool-cond`](./lerp-on-bool-cond.md) _(stub)_
- [`manual-distance`](./manual-distance.md) _(stub)_
- [`manual-mad-decomposition`](./manual-mad-decomposition.md) _(stub)_
- [`manual-reflect`](./manual-reflect.md) _(stub)_
- [`manual-refract`](./manual-refract.md) _(stub)_
- [`manual-smoothstep`](./manual-smoothstep.md) _(stub)_
- [`manual-step`](./manual-step.md) _(stub)_
- [`mul-identity`](./mul-identity.md) _(stub)_
- [`pow-base-two-to-exp2`](./pow-base-two-to-exp2.md) _(stub)_
- [`pow-const-squared`](./pow-const-squared.md)
- [`pow-integer-decomposition`](./pow-integer-decomposition.md) _(stub)_
- [`pow-to-mul`](./pow-to-mul.md) _(stub)_
- [`precise-missing-on-iterative-refine`](./precise-missing-on-iterative-refine.md) _(stub)_
- [`redundant-unorm-snorm-conversion`](./redundant-unorm-snorm-conversion.md) _(stub)_
- [`select-vs-lerp-of-constant`](./select-vs-lerp-of-constant.md) _(stub)_
- [`sin-cos-pair`](./sin-cos-pair.md) _(stub)_
- [`sqrt-of-potentially-negative`](./sqrt-of-potentially-negative.md) _(stub)_
- [`wavereadlaneat-constant-zero-to-readfirst`](./wavereadlaneat-constant-zero-to-readfirst.md) _(stub)_

</details>

<details>
<summary><strong>memory</strong> (5 rules)</summary>

- [`live-state-across-traceray`](./live-state-across-traceray.md) _(stub)_
- [`redundant-texture-sample`](./redundant-texture-sample.md) _(stub)_
- [`sample-use-no-interleave`](./sample-use-no-interleave.md) _(stub)_
- [`scratch-from-dynamic-indexing`](./scratch-from-dynamic-indexing.md) _(stub)_
- [`vgpr-pressure-warning`](./vgpr-pressure-warning.md) _(stub)_

</details>

<details>
<summary><strong>mesh</strong> (9 rules)</summary>

- [`as-payload-over-16k`](./as-payload-over-16k.md) _(stub)_
- [`dispatchmesh-grid-too-small-for-wave`](./dispatchmesh-grid-too-small-for-wave.md) _(stub)_
- [`dispatchmesh-not-called`](./dispatchmesh-not-called.md) _(stub)_
- [`meshlet-vertex-count-bad`](./meshlet-vertex-count-bad.md) _(stub)_
- [`mesh-numthreads-over-128`](./mesh-numthreads-over-128.md) _(stub)_
- [`mesh-output-decl-exceeds-256`](./mesh-output-decl-exceeds-256.md) _(stub)_
- [`output-count-overrun`](./output-count-overrun.md) _(stub)_
- [`primcount-overrun-in-conditional-cf`](./primcount-overrun-in-conditional-cf.md) _(stub)_
- [`setmeshoutputcounts-in-divergent-cf`](./setmeshoutputcounts-in-divergent-cf.md) _(stub)_

</details>

<details>
<summary><strong>misc</strong> (3 rules)</summary>

- [`compare-equal-float`](./compare-equal-float.md) _(stub)_
- [`comparison-with-nan-literal`](./comparison-with-nan-literal.md) _(stub)_
- [`redundant-precision-cast`](./redundant-precision-cast.md) _(stub)_

</details>

<details>
<summary><strong>opacity-micromaps</strong> (3 rules)</summary>

- [`omm-allocaterayquery2-non-const-flags`](./omm-allocaterayquery2-non-const-flags.md) _(stub)_
- [`omm-rayquery-force-2state-without-allow-flag`](./omm-rayquery-force-2state-without-allow-flag.md) _(stub)_
- [`omm-traceray-force-omm-2state-without-pipeline-flag`](./omm-traceray-force-omm-2state-without-pipeline-flag.md) _(stub)_

</details>

<details>
<summary><strong>packed-math</strong> (6 rules)</summary>

- [`manual-f32tof16`](./manual-f32tof16.md) _(stub)_
- [`min16float-in-cbuffer-roundtrip`](./min16float-in-cbuffer-roundtrip.md) _(stub)_
- [`min16float-opportunity`](./min16float-opportunity.md) _(stub)_
- [`pack-clamp-on-prove-bounded`](./pack-clamp-on-prove-bounded.md) _(stub)_
- [`pack-then-unpack-roundtrip`](./pack-then-unpack-roundtrip.md) _(stub)_
- [`unpack-then-repack`](./unpack-then-repack.md) _(stub)_

</details>

<details>
<summary><strong>rdna4</strong> (3 rules)</summary>

- [`oriented-bbox-not-set-on-rdna4`](./oriented-bbox-not-set-on-rdna4.md) _(stub)_
- [`rga-pressure-bridge-stub`](./rga-pressure-bridge-stub.md) _(stub)_
- [`wave64-on-rdna4-compute-misses-dynamic-vgpr`](./wave64-on-rdna4-compute-misses-dynamic-vgpr.md) _(stub)_

</details>

<details>
<summary><strong>sampler-feedback</strong> (3 rules)</summary>

- [`feedback-every-sample`](./feedback-every-sample.md) _(stub)_
- [`feedback-write-wrong-stage`](./feedback-write-wrong-stage.md) _(stub)_
- [`sampler-feedback-without-streaming-flag`](./sampler-feedback-without-streaming-flag.md) _(stub)_

</details>

<details>
<summary><strong>saturate-redundancy</strong> (5 rules)</summary>

- [`clamp01-to-saturate`](./clamp01-to-saturate.md) _(stub)_
- [`redundant-abs`](./redundant-abs.md) _(stub)_
- [`redundant-normalize`](./redundant-normalize.md) _(stub)_
- [`redundant-saturate`](./redundant-saturate.md) _(stub)_
- [`redundant-transpose`](./redundant-transpose.md) _(stub)_

</details>

<details>
<summary><strong>ser</strong> (12 rules)</summary>

- [`coherence-hint-encodes-shader-type`](./coherence-hint-encodes-shader-type.md) _(stub)_
- [`coherence-hint-redundant-bits`](./coherence-hint-redundant-bits.md) _(stub)_
- [`fromrayquery-invoke-without-shader-table`](./fromrayquery-invoke-without-shader-table.md) _(stub)_
- [`hitobject-construct-outside-allowed-stages`](./hitobject-construct-outside-allowed-stages.md) _(stub)_
- [`hitobject-invoke-after-recursion-cap`](./hitobject-invoke-after-recursion-cap.md) _(stub)_
- [`hitobject-passed-to-non-inlined-fn`](./hitobject-passed-to-non-inlined-fn.md) _(stub)_
- [`hitobject-stored-in-memory`](./hitobject-stored-in-memory.md) _(stub)_
- [`maybereorderthread-outside-raygen`](./maybereorderthread-outside-raygen.md) _(stub)_
- [`maybereorderthread-without-payload-shrink`](./maybereorderthread-without-payload-shrink.md) _(stub)_
- [`reordercoherent-uav-missing-barrier`](./reordercoherent-uav-missing-barrier.md) _(stub)_
- [`ser-coherence-hint-bits-overflow`](./ser-coherence-hint-bits-overflow.md) _(stub)_
- [`ser-trace-then-invoke-without-reorder`](./ser-trace-then-invoke-without-reorder.md) _(stub)_

</details>

<details>
<summary><strong>sm6_10</strong> (4 rules)</summary>

- [`cluster-id-without-cluster-geometry-feature-check`](./cluster-id-without-cluster-geometry-feature-check.md) _(stub)_
- [`getgroupwaveindex-without-wavesize-attribute`](./getgroupwaveindex-without-wavesize-attribute.md) _(stub)_
- [`groupshared-over-32k-without-attribute`](./groupshared-over-32k-without-attribute.md) _(stub)_
- [`reference-data-type-not-supported-pre-sm610`](./reference-data-type-not-supported-pre-sm610.md) _(stub)_

</details>

<details>
<summary><strong>texture</strong> (13 rules)</summary>

- [`anisotropy-without-anisotropic-filter`](./anisotropy-without-anisotropic-filter.md) _(stub)_
- [`bgra-rgba-swizzle-mismatch`](./bgra-rgba-swizzle-mismatch.md) _(stub)_
- [`comparison-sampler-without-comparison-op`](./comparison-sampler-without-comparison-op.md) _(stub)_
- [`gather-channel-narrowing`](./gather-channel-narrowing.md) _(stub)_
- [`gather-cmp-vs-manual-pcf`](./gather-cmp-vs-manual-pcf.md) _(stub)_
- [`manual-srgb-conversion`](./manual-srgb-conversion.md) _(stub)_
- [`mip-clamp-zero-on-mipped-texture`](./mip-clamp-zero-on-mipped-texture.md) _(stub)_
- [`samplecmp-vs-manual-compare`](./samplecmp-vs-manual-compare.md) _(stub)_
- [`samplegrad-with-constant-grads`](./samplegrad-with-constant-grads.md) _(stub)_
- [`samplelevel-with-zero-on-mipped-tex`](./samplelevel-with-zero-on-mipped-tex.md) _(stub)_
- [`texture-array-known-slice-uniform`](./texture-array-known-slice-uniform.md) _(stub)_
- [`texture-as-buffer`](./texture-as-buffer.md) _(stub)_
- [`texture-lod-bias-without-grad`](./texture-lod-bias-without-grad.md) _(stub)_

</details>

<details>
<summary><strong>vrs</strong> (4 rules)</summary>

- [`sv-depth-vs-conservative-depth`](./sv-depth-vs-conservative-depth.md) _(stub)_
- [`vrs-incompatible-output`](./vrs-incompatible-output.md) _(stub)_
- [`vrs-rate-conflict-with-target`](./vrs-rate-conflict-with-target.md) _(stub)_
- [`vrs-without-perprimitive-or-screenspace-source`](./vrs-without-perprimitive-or-screenspace-source.md) _(stub)_

</details>

<details>
<summary><strong>wave-helper-lane</strong> (7 rules)</summary>

- [`quadany-quadall-non-quad-stage`](./quadany-quadall-non-quad-stage.md) _(stub)_
- [`quadany-replaceable-with-derivative-uniform-branch`](./quadany-replaceable-with-derivative-uniform-branch.md) _(stub)_
- [`startvertexlocation-not-vs-input`](./startvertexlocation-not-vs-input.md) _(stub)_
- [`waveops-include-helper-lanes-on-non-pixel`](./waveops-include-helper-lanes-on-non-pixel.md) _(stub)_
- [`wave-reduction-pixel-without-helper-attribute`](./wave-reduction-pixel-without-helper-attribute.md) _(stub)_
- [`wavesize-fixed-on-sm68-target`](./wavesize-fixed-on-sm68-target.md) _(stub)_
- [`wavesize-range-disordered`](./wavesize-range-disordered.md) _(stub)_

</details>

<details>
<summary><strong>work-graphs</strong> (6 rules)</summary>

- [`mesh-node-missing-output-topology`](./mesh-node-missing-output-topology.md) _(stub)_
- [`mesh-node-not-leaf`](./mesh-node-not-leaf.md) _(stub)_
- [`mesh-node-uses-vertex-shader-pipeline`](./mesh-node-uses-vertex-shader-pipeline.md) _(stub)_
- [`nodeid-implicit-mismatch`](./nodeid-implicit-mismatch.md) _(stub)_
- [`outputcomplete-missing`](./outputcomplete-missing.md) _(stub)_
- [`quad-or-derivative-in-thread-launch-node`](./quad-or-derivative-in-thread-launch-node.md) _(stub)_

</details>

<details>
<summary><strong>workgroup</strong> (20 rules)</summary>

- [`compute-dispatch-grid-shape-vs-quad`](./compute-dispatch-grid-shape-vs-quad.md) _(stub)_
- [`groupshared-16bit-unpacked`](./groupshared-16bit-unpacked.md) _(stub)_
- [`groupshared-atomic-replaceable-by-wave`](./groupshared-atomic-replaceable-by-wave.md) _(stub)_
- [`groupshared-dead-store`](./groupshared-dead-store.md) _(stub)_
- [`groupshared-first-read-without-barrier`](./groupshared-first-read-without-barrier.md) _(stub)_
- [`groupshared-overwrite-before-barrier`](./groupshared-overwrite-before-barrier.md) _(stub)_
- [`groupshared-stride-32-bank-conflict`](./groupshared-stride-32-bank-conflict.md) _(stub)_
- [`groupshared-stride-non-32-bank-conflict`](./groupshared-stride-non-32-bank-conflict.md) _(stub)_
- [`groupshared-too-large`](./groupshared-too-large.md) _(stub)_
- [`groupshared-union-aliased`](./groupshared-union-aliased.md) _(stub)_
- [`groupshared-volatile`](./groupshared-volatile.md) _(stub)_
- [`groupshared-when-registers-suffice`](./groupshared-when-registers-suffice.md) _(stub)_
- [`groupshared-write-then-no-barrier-read`](./groupshared-write-then-no-barrier-read.md) _(stub)_
- [`interlocked-bin-without-wave-prereduce`](./interlocked-bin-without-wave-prereduce.md) _(stub)_
- [`interlocked-float-bit-cast-trick`](./interlocked-float-bit-cast-trick.md) _(stub)_
- [`numthreads-not-wave-aligned`](./numthreads-not-wave-aligned.md) _(stub)_
- [`numthreads-too-small`](./numthreads-too-small.md) _(stub)_
- [`numwaves-anchored-cap`](./numwaves-anchored-cap.md) _(stub)_
- [`wave-prefix-sum-vs-scan-with-atomics`](./wave-prefix-sum-vs-scan-with-atomics.md) _(stub)_
- [`wavesize-attribute-missing`](./wavesize-attribute-missing.md) _(stub)_

</details>

<details>
<summary><strong>xe2</strong> (1 rule)</summary>

- [`wavesize-32-on-xe2-misses-simd16`](./wavesize-32-on-xe2-misses-simd16.md) _(stub)_

</details>

---

## Conventions for contributors

Every shipped rule has a corresponding `docs/blog/<rule-id>.md` post.
Stubs are scaffolds, not authoritative content; the rule page at
`docs/rules/<rule-id>.md` is canonical. Promote a stub to a full post by
replacing the referral sections with prose written graphics-engineer to
graphics-engineer. Target length 800-1500 words. License CC-BY-4.0.

See [`pow-const-squared`](./pow-const-squared.md) for a reference full-length post.
