# SM 6.7 / 6.8 / 6.9 rule expansion — research brain-dump

Date: 2026-04-30
Author: shader-clippy SM-features research agent
Status: source material for ADR 0010

## Context

The existing ROADMAP and ADR 0007 cover the modern shader-model surface up to SM 6.8 ([work graphs intro], packed math, sampler feedback, mesh / amplification, DXR 1.1, descriptor heaps, basic VRS). What they don't cover: the wave of features that landed with **DXR 1.2 + SM 6.9** retail (Agility SDK 1.619, [shipped Feb 2026][sm69-retail]) and the SM 6.7 helper-lane / quad surface that an HLSL linter ought to be opinionated about. The big-ticket SM 6.9 deltas are **Shader Execution Reordering (SER)**, **Cooperative Vectors**, **Long Vectors**, **Opacity Micromaps**, **Mesh Nodes in Work Graphs** (preview), the **WaveSize range** attribute (SM 6.8), and a small but landmine-rich set of payload-access-qualifier / `WaveOpsIncludeHelperLanes` / `QuadAny`/`QuadAll` rules from SM 6.7. Each surface has its own footguns; this document mines them for portable, observable, blog-ready lint candidates.

This is the first SM-feature research pass since ADR 0007. The bar is the same: a candidate ships only if (a) the wrong pattern is observable from AST + Slang reflection + a CFG, (b) the right pattern is unambiguous, (c) the GPU reason fits in one paragraph.

[sm69-retail]: https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/

## Methodology

Sources consulted:

- DirectX-Specs SM 6.7 / 6.8 / 6.9 spec pages on `microsoft.github.io/DirectX-Specs` and the underlying `microsoft/hlsl-specs` proposal repo (proposals 0013 wave-size-range, 0015 extended-command-info, 0024 opacity-micromaps, 0027 SER, 0029 cooperative-vector, 0030 dxil-vectors, 0044 sm69-required-features, 0045 clustered-geometry).
- DirectX dev blog announcement posts for SM 6.9 retail, OMM, SER, Cooperative Vector, mesh nodes in work graphs, native + long vectors, work graphs.
- NVIDIA technical blog: SER best practices, Indiana Jones path-tracing case study, WMMA / RDNA-vs-NV linear-swept-spheres background.
- AMD GPUOpen: work-graphs mesh-nodes tutorial, RDNA 4 matrix cores intro, WMMA RDNA3 article.
- Khronos blog: `VK_EXT_ray_tracing_invocation_reorder` (SER cross-vendor analogue) — confirms coherence-hint MSB-priority and live-state guidance.
- Slang docs: `shader-execution-reordering.md` confirms what Slang's reflection / IR already exposes.

Exclusion criteria:

1. **Already in ROADMAP or ADR 0007** — every SM 6.6/6.7/6.8 rule listed there is dropped from this brainstorm. Cross-checked: `descriptor-heap-no-non-uniform-marker`, `descriptor-heap-type-confusion`, `mesh-numthreads-over-128`, `mesh-output-decl-exceeds-256`, `as-payload-over-16k`, `setmeshoutputcounts-in-divergent-cf`, `nodeid-implicit-mismatch`, `outputcomplete-missing`, `quad-or-derivative-in-thread-launch-node`, `wave-intrinsic-helper-lane-hazard`, `vrs-incompatible-output`, `feedback-write-wrong-stage`, `feedback-every-sample`, `tracerray-conditional`, `anyhit-heavy-work`, `inline-rayquery-when-pipeline-better`, `live-state-across-traceray`, `missing-ray-flag-cull-non-opaque`. None of the new candidates duplicate these.
2. **Vendor-only** — features that exist only in NVAPI / VK_NV / OptiX with no DirectX-Specs counterpart in SM 6.9 (Linear-Swept Spheres, Disjoint Orthogonal Triangle Strips). Slang exposes them as NV-only methods on `HitObject`, but they aren't shippable lints in a portable D3D linter today. Tracked as **deferred**.
3. **Spec-pending** — clustered geometry / CBLAS (`ClusterID()`, `RAYTRACING_PIPELINE_FLAG_ALLOW_CLUSTERED_GEOMETRY`) is `Accepted` but explicitly targets **SM 6.10**, not 6.9. We note it but don't plan implementation.
4. **Not statically observable** — runtime payload size at a `HitObject::Invoke()` call site can mismatch the actual closesthit's expected payload, but only the *call* is in the shader; the matched shader lives in a different translation unit. Skipped.

Each candidate below has been validated against ROADMAP.md (current state — not the older draft preserved in this worktree) and ADR 0007.

## Candidate rules

### Shader Execution Reordering (SER) — SM 6.9, DXR 1.2

The whole SER surface (`dx::HitObject`, `dx::MaybeReorderThread`, `[reordercoherent]`, `Barrier(UAV_MEMORY, REORDER_SCOPE)`) is brand-new. ADR 0007 has **zero** SER rules. This is the largest pack and the highest-leverage one for the project's "early on SM 6.9 perf rules" reputation play.

1. **`hitobject-stored-in-memory`** — `dx::HitObject` written into a `(RW)ByteAddressBuffer`, `(RW)StructuredBuffer`, groupshared, or assigned into a ray-payload field. Spec is explicit: *"Cannot be stored in memory buffers, groupshared memory, ray payloads, or intersection attributes."* Storage size is unspecified and intangible. Diagnostic class: hard error / UB. Phase: 3 (reflection-aware: Slang reflection knows the resource type at the assignment site). Source: <https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_9.html>, <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md>.

2. **`maybereorderthread-outside-raygen`** — `dx::MaybeReorderThread` called from a closesthit, anyhit, miss, callable, or non-raytracing entry. Spec restricts the call to raygeneration. Should be a hard error in any tool. Phase: 3 (needs the entry-point stage from Slang reflection). Source: spec 0027.

3. **`hitobject-construct-outside-allowed-stages`** — `HitObject::TraceRay`, `HitObject::FromRayQuery`, `HitObject::MakeMiss`, `HitObject::MakeNop` invoked outside `raygeneration`, `closesthit`, or `miss`. Spec: *"available only in the following shader stages: raygeneration, closesthit, miss."* Phase: 3.

4. **`hitobject-passed-to-non-inlined-fn`** — `HitObject` value passed across a non-inlinable function boundary. Spec restricts pass-by-value to inlined functions; the linter has to detect any user-defined function that takes / returns a `HitObject` and check it's marked inline-eligible. Diagnostic class: UB. Phase: 4 (needs inter-procedural reasoning). Source: spec 0027.

5. **`maybereorderthread-without-payload-shrink`** — `MaybeReorderThread` invoked while a "large" set of locals is live across the call (heuristic: > 128 bytes of estimated live-state across the reorder point). NVIDIA's Indiana Jones case study moved spilled live-state from 84 → 68 bytes and went from 11% → 24% GPU-time savings; the cost of live state across the reorder is the dominant SER overhead. Phase: 7 (IR-level live-range analysis — same machinery as `live-state-across-traceray`). Source: <https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/>.

6. **`coherence-hint-redundant-bits`** — `MaybeReorderThread(hitObject, hint, numBits)` where `numBits` constant is greater than what the hint actually distinguishes (e.g. `hint = materialID & 0xff` but `numBits = 16`). Spec says *"implementations may ignore excess bits"*; padding the hint reduces grouping efficiency. Phase: 4 (needs constant + bit-range analysis). Source: spec 0027 §4.

7. **`coherence-hint-encodes-shader-type`** — coherence hint encodes hit/miss or shader-type information that the implementation **already** prioritizes higher than the user hint. Khronos's blog explicitly calls this out: *"Don't encode shader type or hit/miss status in coherence hints — the system already prioritizes shader routing."* The lint pattern: hint is derived from `IsHit()` / `GetShaderTableIndex()` returns. Phase: 4. Source: <https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder>.

8. **`reordercoherent-uav-missing-barrier`** — UAV is read on one side of a reorder point and written on the other, but either (a) the resource isn't declared `[reordercoherent]` or (b) there is no `Barrier(UAV_MEMORY, REORDER_SCOPE)` between the write and the reorder point. The spec mandates both. Diagnostic class: data race / UB. Phase: 4 (needs a CFG that crosses the reorder point). Source: spec 0027 "Memory Coherence" section.

9. **`hitobject-invoke-after-recursion-cap`** — `HitObject::Invoke` or `HitObject::TraceRay` reachable from a path where the trace recursion depth has already hit `MaxTraceRecursionDepth`. Both calls count toward the budget. Statically detectable for the constant-recursion-depth case (e.g. ray-gen depth 1, closesthit calls `Invoke` then `TraceRay` then `Invoke`). Phase: 4 (needs a call-graph + recursion accounting). Source: spec 0027 "Recursion Depth".

10. **`fromrayquery-invoke-without-shader-table`** — `HitObject::FromRayQuery(...)` followed by `Invoke(...)` without a `SetShaderTableIndex(...)` on the path. Spec: *"Calling Invoke without SetShaderTableIndex on FromRayQuery results in no-op shader execution"*. Subtle — looks correct, silently does nothing. Phase: 4.

11. **`ser-trace-then-invoke-without-reorder`** — `HitObject::TraceRay` immediately followed by `HitObject::Invoke` without an intervening `MaybeReorderThread`. This is the **point** of SER; doing TraceRay+Invoke back-to-back is equivalent to a vanilla `TraceRay` and forfeits the reordering. Pattern is a clear missed-opportunity. Phase: 4 (CFG to confirm no reorder reachable on the path). Source: dev-blog SER article and NVIDIA SER best practices.

### Cooperative Vectors / matrix-vector mul — SM 6.9 (`__builtin_*`)

The Cooperative Vector intrinsics (`MatrixVectorMul`, `MatrixVectorMulAdd`, `OuterProductAccumulate`, `VectorAccumulate`) and their alignment / layout requirements are net-new for SM 6.9. ADR 0007 has nothing in this category.

12. **`coopvec-non-optimal-matrix-layout`** — `MatrixVectorMul` / `MatrixVectorMulAdd` called with `RowMajor` or `ColumnMajor` layout where the matrix is loaded once per shader and reused across many threads in a wave. Spec describes `MulOptimal` and `OuterProductOptimal` as *"opaque implementation specific layout"* that is the only one the hardware tensor / WMMA core can consume directly; row-major works for correctness but skips the fast path. Phase: 3 (Slang reflection sees the `DXILMatrixLayout` enum at the call site). Source: <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0029-cooperative-vector.md>.

13. **`coopvec-fp8-with-non-optimal-layout`** — `F8_E4M3` or `F8_E5M2` matrix interpretation paired with `RowMajor` / `ColumnMajor` layout. Spec: *"Only Optimal layouts can be used with Float8 (E4M3 and E5M2) MatrixInterpretation."* This is a hard validation error, not a perf warning. Phase: 3. Source: spec 0029 minimum-support-set table.

14. **`coopvec-stride-mismatch`** — for row/column-major: stride argument not 16-byte aligned, or stride argument non-zero for an Optimal layout (Optimal layouts mandate `stride = 0`). Phase: 3 (constant-folding the stride argument).

15. **`coopvec-base-offset-misaligned`** — matrix base address + offset combined alignment isn't a multiple of 128 bytes (matrix) or 64 bytes (bias vector). Spec mandates both. Phase: 3 (Slang reflection + constant offset).

16. **`coopvec-non-uniform-matrix-handle`** — matrix handle, matrix offset, matrix interpretation, layout, stride, transpose, or bias offset is non-uniform across the wave. Spec: *"Implementations may enable optimized fast-paths when ... vectors to cooperate behind the scenes in cases with uniform paths, fully occupied waves and uniform values for Matrix, Matrix Offset, Matrix Interpretation, Matrix Layout, Matrix Stride, Matrix Transpose and Bias, Bias Offset, Bias Interpretation."* Per-thread divergent matrix arguments correctness-pass but bypass the WMMA / tensor-core acceleration. Phase: 4 (uniformity analysis). Source: spec 0029.

17. **`coopvec-transpose-without-feature-check`** — `MatrixVectorMul` with the transpose flag set without an `IsCooperativeVectorTransposeSupported` feature check. Spec: *"transpose support isn't guaranteed and needs to be checked explicitly"* even for the minimum-support-set type combinations. This is a runtime-failure footgun, not a perf one. Phase: 3.

### Long Vectors — SM 6.9

18. **`long-vector-in-cbuffer-or-signature`** — `vector<T, N>` with `N > 4` declared in a `cbuffer`, as an entry-point input/output, or as the return type / parameter of an entry function. Spec: *"They cannot be used in shader signatures, constant buffers, typed buffer, or texture types."* Diagnostic class: compile error in DXC, but Slang may surface it later — worth catching early. Phase: 3 (Slang reflection identifies cbuffer / entry-point boundary). Source: <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0030-dxil-vectors.md>, <https://devblogs.microsoft.com/directx/hlsl-native-and-long-vectors/>.

19. **`long-vector-typed-buffer-load`** — long vector loaded from a `Buffer<T>` / `Texture2D<T>` / `RWTexture*<T>` (typed resource). Spec: *"Only loadable and storable from and to raw buffers"* — i.e. only `ByteAddressBuffer` and `StructuredBuffer<vector<T,N>>`. Phase: 3. Source: dev-blog long-vectors article.

20. **`long-vector-non-elementwise-intrinsic`** — long vector passed to a non-elementwise intrinsic (`cross`, `length`, `normalize`, `transpose`, `mul` with matrix). Spec restricts long vectors to elementwise operations + `dot` + `mad` + standard math. `cross` is explicitly called out as not elementwise. Phase: 2 (AST-only — intrinsic name + vector type). Source: spec 0030.

21. **`long-vector-bytebuf-load-misaligned`** — `BAB.Load<vector<T,N>>(offset)` where `offset` is constant and not a multiple of `sizeof(T)` (the per-component minimum) — note that the spec is *per-component-16-byte-robust* but only *scalar-aligned* on the offset, so the lint is conservative: warn when alignment is < 16 for `N >= 4` and we're calling Load on a vector. Phase: 3 (constant offset + vector size).

### Opacity Micromaps — SM 6.9, DXR 1.2

ADR 0007 has nothing on OMM. The pipeline-flag / RayQuery-flag matrix has clear footguns.

22. **`omm-rayquery-force-2state-without-allow-flag`** — `RayQuery<RAY_FLAG_FORCE_OMM_2_STATE, ...>` template-instantiated without `RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS` in the second template parameter. Spec: *"When `RAY_FLAG_FORCE_OMM_2_STATE` is used in a RayQuery's first template argument, the second template argument must have `RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS` set."* Diagnostic class: hard error. Phase: 3 (template-arg parsing). Source: <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0024-opacity-micromaps.md>.

23. **`omm-allocaterayquery2-non-const-flags`** — `AllocateRayQuery2(constRayFlags, RayQueryFlags)` called with a non-constant value for either argument. Spec: *"constRayFlags argument of AllocateRayQuery and constRayFlags and RayQueryFlags arguments of AllocateRayQuery2 must be constant"*. Phase: 3 (constant-fold).

24. **`omm-traceray-force-omm-2state-without-pipeline-flag`** — pipeline-style `TraceRay(..., RAY_FLAG_FORCE_OMM_2_STATE, ...)` from a shader compiled without `RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS`. Spec: *"if a triangle with an OMM is encountered during traversal with this flag cleared, behavior is undefined."* Project-level diagnostic — needs the pipeline-config subobject from Slang reflection. Phase: 3.

### Wave intrinsics — SM 6.7 helper-lane surface

ADR 0007 has `wave-intrinsic-helper-lane-hazard` (PS wave intrinsics after potential `discard`) but **does not** cover the SM 6.7 `WaveOpsIncludeHelperLanes` attribute or the `QuadAny` / `QuadAll` intrinsics that change the rules.

25. **`waveops-include-helper-lanes-on-non-pixel`** — `[WaveOpsIncludeHelperLanes]` attribute on a non-pixel-shader entry. Spec: *"The compiler will issue a warning if it is present in any other type of shader"* — but that's a DXC warning, and it's only a warning. We promote to a hard rule. Phase: 3 (Slang reflection sees the attribute + entry stage). Source: <https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_7_Wave_Ops_Include_Helper_Lanes.html>.

26. **`wave-reduction-pixel-without-helper-attribute`** — `WaveActiveSum` / `WaveActiveCountBits` / `WavePrefixSum` and similar reductions in a pixel shader where the result is used to drive control flow that *must* match the quad-uniform expectation, **without** `[WaveOpsIncludeHelperLanes]`. The footgun is the inverse of `wave-intrinsic-helper-lane-hazard`: the existing rule warns about helper lanes accidentally participating; this rule warns about helper lanes *expected to participate but not doing so* by default. Calls out the SM 6.7 attribute as a fix. Phase: 4 (data-flow: the result of the reduction must reach a derivative-bearing op). Source: spec link above + Wave-Intrinsics wiki.

27. **`quadany-quadall-non-quad-stage`** — `QuadAny` / `QuadAll` invoked from a stage outside `pixel`, `compute`, `mesh`, or `amplification`. Spec restricts these intrinsics to those four stages. Phase: 3. Source: <https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_7_QuadAny_QuadAll.html>.

28. **`quadany-replaceable-with-derivative-uniform-branch`** — a `[branch]` over a quad-uniform condition where the body contains a single `Sample` / derivative op, and the predicate is a per-lane condition that is **already** quad-uniform — we should suggest `if (QuadAny(cond))` to keep helper lanes alive across the sample. The opposite (using `QuadAny` where each lane individually needs the work) is also worth a counter-rule, but that's harder to detect. Phase: 4. Source: SM 6.7 QuadAny/QuadAll spec.

### SM 6.8 / 6.9 attribute rules

29. **`wavesize-range-disordered`** — `[WaveSize(min, max [, preferred])]` where `min > max`, or `min`/`max` not powers of two in the [4, 128] range, or `preferred < min` / `preferred > max`. Spec: *"The first element is not zero. The second element is greater than the first element or zero. The third element is greater or equal to the first element and less than or equal to the second element or zero."* DXC catches some of this but a linter that surfaces it as a structured diagnostic with a quick-fix is more useful. Phase: 2 (AST + constant-fold). Source: <https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_WaveSize.html>, proposal 0013.

30. **`wavesize-fixed-on-sm68-target`** — single-arg `[WaveSize(N)]` on a shader that targets SM 6.8+. The range form `[WaveSize(min, max[, preferred])]` is strictly more flexible (driver picks the best wave size on hardware that supports a range, e.g. RDNA wave32/wave64). Suggestion-only. Phase: 3 (target shader-model from compile-options).

31. **`startvertexlocation-not-vs-input`** — `SV_StartVertexLocation` / `SV_StartInstanceLocation` declared on a non-VS entry input (or as VS *output*). Spec: *"The system only populates these values as inputs to the vertex shader. For any subsequent stage that cares about them, the shader must pass them manually using a user semantic."* Phase: 3 (reflection sees the semantic + the entry stage).

### Mesh Nodes in Work Graphs — SM 6.9 (preview)

ADR 0007 has `nodeid-implicit-mismatch`, `outputcomplete-missing`, `quad-or-derivative-in-thread-launch-node`. Mesh nodes specifically need their own rules.

32. **`mesh-node-not-leaf`** — a node with `[NodeLaunch("mesh")]` that has any `NodeOutput<T>` / `EmptyNodeOutput` children. Spec: *"Mesh launch nodes can only appear at a leaf of a work graph."* Phase: 3 (Slang reflection enumerates the node graph). Source: <https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html>, <https://devblogs.microsoft.com/directx/d3d12-mesh-nodes-in-work-graphs/>. Spec status: announced as preview in Agility SDK 1.717+; the leaf-only restriction is firm.

33. **`mesh-node-missing-output-topology`** — `[NodeLaunch("mesh")]` declared without `[OutputTopology(...)]`, `[NumThreads(...)]`, or one of `[NodeDispatchGrid]` / `SV_DispatchGrid`. Mesh nodes inherit the mesh-shader requirement matrix. Phase: 3.

34. **`mesh-node-uses-vertex-shader-pipeline`** — work-graph entry expects to feed into a traditional VS-bearing pipeline. Spec: *"Programs that start with a vertex shader ... are not supported in work graphs."* Phase: 3 (cross-references PSO / state-object reflection if available; otherwise warn on the shader-side IO pattern).

### Cross-cutting / numerical safety — SM 6.9 16-bit specials

35. **`isspecialfloat-implicit-fp16-promotion`** — `isinf(half)` / `isnan(half)` / `isfinite(half)` calls compiled against a target older than SM 6.9 where the 16-bit overload doesn't exist; spec says these implicitly cast to fp32. Suggestion: target SM 6.9 to get the native 16-bit overload, otherwise be explicit about the cast. Phase: 3 (target version + arg type). Source: <https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_9.html>.

36. **`isnormal-pre-sm69`** — `isnormal(x)` used while compiling for SM 6.8 or older. The intrinsic only exists in SM 6.9. DXC is the authoritative compiler for that error today, but in a build pipeline that target-switches between SMs (common for fallback paths), surfacing it at lint time is useful. Phase: 3.

## Excluded / deferred

- **Linear-Swept Spheres / DOTS / RTXCR** — vendor-only (NVAPI / VK_NV / OptiX), not in DirectX-Specs SM 6.9 spec. Slang exposes `GetSpherePositionAndRadius` / `GetLssPositionsAndRadii` on `HitObject` as **NV-only** methods. Defer until a portable D3D analogue ships. Tracked: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_ray_tracing_linear_swept_spheres.html>.
- **Triangle Clusters / CBLAS / `ClusterID()`** — proposal 0045 is `Accepted` but explicitly targets SM 6.10. Add to the SM 6.10 rule pack later. Source: <https://github.com/microsoft/hlsl-specs/blob/main/proposals/0045-clustered-geometry.md>.
- **Sampler Feedback v2** — no v2 surface in SM 6.7/6.8/6.9. Existing ADR 0007 rules (`feedback-write-wrong-stage`, `feedback-every-sample`) cover the shipped surface.
- **Variable Rate Shading per-primitive in mesh shader** — already covered by ADR 0007 `vrs-incompatible-output`. The SM 6.4 `MeshShaderPerPrimitiveShadingRateSupported` cap is a runtime check, not a static one we can lint.
- **Bindless tier 4 enhancements / new ResourceDescriptorHeap forms** — ADR 0007 already has `descriptor-heap-no-non-uniform-marker` and `descriptor-heap-type-confusion`. No SM 6.9 delta worth a new rule found.
- **`HitObject::Invoke` payload-type mismatch with the bound closesthit** — the closesthit is in a separate compilation; no static observation.
- **`MaybeReorderThread` runtime-divergence reordering** — the spec explicitly says reordering is best-effort; threads that don't arrive at the reorder point don't migrate. Hard to lint without runtime telemetry.

## Phase distribution

- **Phase 2 (AST-only):** rules #20, #29 — 2 rules.
- **Phase 3 (reflection / type-aware):** rules #1, #2, #3, #12, #13, #14, #15, #17, #18, #19, #21, #22, #23, #24, #25, #27, #30, #31, #32, #33, #34, #35, #36 — 23 rules.
- **Phase 4 (control flow + light data flow):** rules #4, #6, #7, #8, #9, #10, #11, #16, #26, #28 — 10 rules.
- **Phase 7 (IR / live-range):** rule #5 — 1 rule.

Total: **36 candidates**, 0 duplicates against ROADMAP.md or ADR 0007. The pack is heavier on Phase 3 than ADR 0007 was, because most SER / OMM / cooperative-vector / long-vector validation lives at the reflection/type-attribute level, not at the data-flow level.

## Notes for the ADR

- **Spec status warnings**: SER + Cooperative Vector + Long Vectors + OMM all shipped retail with Agility SDK 1.619 (Feb 2026). Mesh nodes in work graphs are *preview* — the lints can be written against the preview surface but should be gated on a `experimental.work-graph-mesh-nodes` config until the API is locked.
- **DXC vs Slang parity**: DXC v1.8.2505+ has SM 6.9 production support. Slang exposes SER as `HitObject` with NV-only and EXT-cross-vendor methods clearly distinguished — this is the correct boundary for our "portable D3D linter" scope. Slang reflection does cover the cooperative-vector intrinsics with the `linalg.h` namespace pattern.
- **Blog-series leverage**: Phase 3 alone gives us four named blog posts — "SER for HLSL: the rules DXC won't tell you", "Cooperative Vectors: the alignment and layout rules that decide whether you hit the tensor core", "Long Vectors: the cbuffer/signature wall and how to route around it", "Opacity Micromaps: the flag matrix that silently turns into UB". Phase 4 adds the SER coherence-hint / barrier post and the wave-helper-lane attribute post.
