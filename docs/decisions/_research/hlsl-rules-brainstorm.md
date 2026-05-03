<!--
date: 2026-04-30
prompt-summary: brainstorm additional HLSL lint rules beyond the existing Phase 0/1 ROADMAP set, covering modern shader-model surface (SM 6.4 → 6.8), descriptor heaps, atomics, mesh / amplification, DXR, sampler feedback, work graphs, and helper-lane / FMA hazards.
preserved-verbatim: yes — see ../0007-rule-pack-expansion.md for the distilled decision and per-phase placement.
note: the agent's heading says "30" but produced 41 rules; all 41 were adopted.
-->

# 30 Additional Rules for shader-clippy
[Note: agent labeled "30" but produced 41. Use all 41.]

## Math / Intrinsics

1. manual-mad-decomposition — math, phase 2. Detects (a*b)+c split across statements losing FMA fold. GPU FMA is single-cycle; broken chains cost extra ALU when the optimizer loses the fold across statement boundaries.
2. dot-on-axis-aligned-vector — math, phase 2. dot(v, float3(1,0,0)) → v.x. Real dot is 3 muls + 2 adds; .x is zero ALU.
3. length-then-divide — math, phase 2. v / length(v) → normalize(v). Manual form pays sqrt then divide; normalize uses rsqrt + multiply.
4. cross-with-up-vector — math, phase 2. cross(v, float3(0,1,0)) collapses to negations + moves; general cross is 6 muls + 3 subs.
5. countbits-vs-manual-popcount — math, phase 2. Hand-rolled popcount → countbits(). Single v_bcnt vs 12-20 ALU.
6. firstbit-vs-log2-trick — math, phase 2. log2((float)x) cast back as MSB lookup → firstbithigh. Single integer op vs quarter-rate transcendental + casts.

## fp16 / packed-math (SM 6.6)

7. pack-then-unpack-roundtrip — math, phase 4. pack_u8(unpack_u8u32(x)) and f32tof16/f16tof32 round-trips. Each conversion is real ALU; round-trip is dead work.
8. pack-clamp-on-prove-bounded — math, phase 3. pack_clamp_u8 where operand provably in [0,255]. Clamp variants emit extra v_med3-style instruction over truncating pack_u8.
9. min16float-in-cbuffer-roundtrip — math, phase 3. min16float param loaded from 32-bit cbuffer field — compiler emits 32→16 demotion every read. Hot loops re-pay the conversion every iteration.
10. dot4add-opportunity — math, phase 4. 4-tap int8/uint8 dot products via shifts/masks/adds → dot4add_u8packed/dot4add_i8packed. SM 6.4 packed dot maps to one DP4a vs 8+ ALU.

## Bindings / Descriptor Heaps (SM 6.6+)

11. descriptor-heap-no-non-uniform-marker — bindings, phase 3. ResourceDescriptorHeap[i] / SamplerDescriptorHeap[i] without NonUniformResourceIndex when divergent. Spec-defined UB without the marker.
12. wave-active-all-equal-precheck — bindings, phase 4. Scalarization opportunity for divergent descriptor index where WaveActiveAllEqual(i) would let one branch use cheap uniform path. Scalar loads are 4x faster on GCN/RDNA.
13. descriptor-heap-type-confusion — bindings, phase 3. Sampler assigned to CBV/SRV/UAV slot or vice versa via wrong heap.
14. cbuffer-divergent-index — bindings, phase 4. cbuffer/ICB read with divergent index. NVIDIA flags this; divergent CBV reads serialize on the constant cache.
15. all-resources-bound-not-set — bindings, phase 3 (project-level). Project compiles without -all-resources-bound while declaring fully-populated root signatures. NVIDIA documents driver opts unlocked by the flag.

## Atomics / Groupshared

16. interlocked-bin-without-wave-prereduce — control-flow, phase 4. InterlockedAdd to small fixed bin set without WaveActiveSum/WavePrefixSum pre-reduction. Pre-reducing within wave drops atomic traffic 32x/64x.
17. interlocked-float-bit-cast-trick — math, phase 4. Hand-rolled asuint/sign-flip dance for atomic min/max on floats → SM 6.6 native InterlockedMin/Max on float. Saves 6 ALU + CAS loop.
18. groupshared-stride-32-bank-conflict — workgroup, phase 4. groupshared float arr[32*N] indexed as arr[tid*32+k]. LDS has 32 banks of 32 bits; stride-32 access serializes wave 32-way. Fix: +1 padding.
19. groupshared-write-then-no-barrier-read — workgroup, phase 4. Thread reads groupshared cell written by another thread without barrier between them. UB. Distinct from existing groupshared-uninitialized-read.

## Texture / Sampling

20. sample-in-loop-implicit-grad — texture, phase 4. Texture.Sample (implicit derivatives) inside loop/conditional/non-uniform function. Spec UB; cross-lane derivative reads can return values from other branches.
21. gather-cmp-vs-manual-pcf — texture, phase 3. 2x2 unrolled SampleCmp for PCF → GatherCmp + manual filter weights. Single texture op vs four serial SampleCmps — 4x bandwidth and TMU win.
22. texture-lod-bias-without-grad — texture, phase 3. SampleBias in compute or non-quad-uniform contexts. Requires implicit derivatives — same UB family as rule 20 in compute.

## Variable Rate Shading / Pixel Shader

23. vrs-incompatible-output — vrs, phase 3. PS writes SV_Depth/SV_StencilRef/discard while pipeline declares per-draw or per-primitive shading rate, or inputs SV_ShadingRate together with SV_SampleIndex/sample interp. Silently forces fine-rate shading.
24. early-z-disabled-by-conditional-discard — control-flow, phase 4. discard/clip reachable from non-uniform CF in PS without [earlydepthstencil]. Turns off early-Z and breaks tiled-renderer fast path.
25. sv-depth-vs-conservative-depth — control-flow, phase 3. PS writes SV_Depth where value is monotonically >= or <= rasterized depth → SV_DepthGreaterEqual/SV_DepthLessEqual. Conservative depth keeps early-Z alive.
26. rov-without-earlydepthstencil — bindings, phase 3. RasterizerOrdered* in PS without [earlydepthstencil] and without depth/discard hazards. ROV serialization may extend across entire shader without the attribute.

## Sampler Feedback (SM 6.5+)

27. feedback-write-wrong-stage — sampler-feedback, phase 3. WriteSamplerFeedback in compute/vertex/non-PS. Spec-restricted to PS.
28. feedback-every-sample — sampler-feedback, phase 4. WriteSamplerFeedback* in hot path with no stochastic gate. Spec recommends discarding 99%+ of writes.

## Mesh / Amplification (SM 6.5)

29. mesh-numthreads-over-128 — mesh, phase 3. [numthreads] on mesh/AS entry with X*Y*Z > 128. Hard spec limit — PSO creation fails.
30. mesh-output-decl-exceeds-256 — mesh, phase 3. out vertices/indices with N or M > 256. Hard spec limit.
31. as-payload-over-16k — mesh, phase 3. Amplification-shader payload struct sized > 16384 bytes (Slang reflection knows layout). PSO creation fails.
32. setmeshoutputcounts-in-divergent-cf — mesh, phase 4. SetMeshOutputCounts reachable from non-thread-uniform CF or called more than once. Spec UB.

## Ray Tracing (DXR)

33. tracerray-conditional — dxr, phase 4. TraceRay/RayQuery::TraceRayInline inside if whose condition isn't trivially uniform. Forces compiler to extend live ranges across the trace and spill to ray-stack memory.
34. anyhit-heavy-work — dxr, phase 4. Any-hit shader doing texture sampling beyond alpha-mask, loops, or lighting. Any-hit runs O(hits-per-ray); shading belongs in closesthit.
35. missing-ray-flag-cull-non-opaque — dxr, phase 3. TraceRay against opaque-only geometry without RAY_FLAG_CULL_NON_OPAQUE. Disables a class of BVH culling on NV/AMD.
36. live-state-across-traceray — dxr, phase 7 (IR). Locals computed before TraceRay and read after — they spill to ray stack.
37. inline-rayquery-when-pipeline-better / pipeline-when-inline-better — dxr, phase 4. Wrong-tool selection — 20-50% perf delta on shadow/AO passes.

## Work Graphs (SM 6.8)

38. nodeid-implicit-mismatch — work-graphs, phase 3. NodeOutput<T> X declarations without explicit [NodeId(...)] when struct/downstream node names disagree.
39. outputcomplete-missing — work-graphs, phase 4. GetGroupNodeOutputRecords/GetThreadNodeOutputRecords not paired with OutputComplete() on every CFG path.
40. quad-or-derivative-in-thread-launch-node — work-graphs, phase 4. QuadAny/QuadReadAcross*/ddx/ddy/implicit-deriv sample inside thread-launch node. No quad structure available.

## Cross-cutting

41. wave-intrinsic-helper-lane-hazard — control-flow, phase 4. Wave intrinsics in PS after potential discard / where helper lanes may participate. Helper lanes contribute to wave reductions unless explicitly excluded. Distinct from existing wave-intrinsic-non-uniform.
