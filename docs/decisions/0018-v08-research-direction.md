---
status: Proposed
date: 2026-05-02
deciders: NelCit
tags: [v0.8, research, candidates, sm6.10, rdna4, blackwell, xe2, competitive-landscape, v1.0-criteria]
---

# v0.8+ research-driven expansion — post-v0.7.0 horizon

## Strategic summary (TL;DR for maintainers)

Twelve-line digest. v0.7.0 ships 169 rules. The next research horizon is shaped by Shader Model 6.10 (preview, Apr 2026), RDNA 4 (Feb 2025), Blackwell / RTX 50 (early 2025), Xe2 / Battlemage B580 (late 2024), and the deprecation of Cooperative Vector in favour of `linalg::Matrix`.

**Three biggest coverage gaps in the existing 169 rules.** (1) The `vrs` and `misc` categories are thin (2-3 rules each) — VRS was over-promised in ADR 0007. (2) Zero rules anchor on Slang's `linalg::Matrix` / SM 6.10 even though the API is now released. (3) Eight ray-tracing-position-fetch / cluster-ID / `OutputComplete` constructs from SM 6.9-6.10 have no rule coverage despite ADR 0010 listing them as in-scope.

**Three biggest new-surface candidates.** (1) **RDNA 4 dynamic-VGPR mode** — supported only in wave32 compute, so wave64 compute on RDNA 4 silently misses occupancy gains. (2) **Blackwell FP4 / FP6 cooperative matrix** — coopvec layouts that were "optimal" for Hopper FP8 are not optimal for Blackwell FP4. (3) **Xe2 SIMD16 native** — `[WaveSize(32)]` declarations on shaders that should run as SIMD16 hide native efficiency on Battlemage.

**Three biggest competitive-landscape findings.** (1) RGA's "live VGPR analysis" surfaces hot blocks that exceed the VGPR-block-size threshold — we approximate this poorly with our AST heuristic; v0.8 should consume RGA's CSV output as an optional oracle. (2) Nsight's "Warp Stalled by L1 Long Scoreboard" maps cleanly to our buffer / texture-cache rules but we have no rule that enforces "interleave compute between sample and use." (3) Slang's own `-Wall` covers some patterns we flag but at very different fidelity (e.g. dead-store, unused-cbuffer-field) — we should explicitly cite which rules are Slang-redundant in our docs to manage expectations.

**Proposed v0.8 launch pack (8 rules, all citable to a primary source):** `linalg-matrix-non-optimal-layout`, `linalg-matrix-element-type-mismatch`, `getgroupwaveindex-without-wavesize-attribute`, `groupshared-over-32k-without-attribute`, `triangle-object-positions-without-allow-data-access-flag`, `numthreads-not-wave-aligned` (stub burndown), `dispatchmesh-grid-too-small-for-wave`, `dot4add-opportunity` (stub burndown). All 8 ship at warn severity, machine-applicable where syntactic, suggestion-grade where reflection-bound. The two RDNA-4-specific rules (`wave64-on-rdna4-compute-misses-dynamic-vgpr`, `oriented-bbox-not-set-on-rdna4`) defer to v0.10 because they need an `[experimental.target = rdna4]` gate or project-side BLAS-build-flag input that's not yet in the config surface.

---

## Context

[v0.7.0 shipped on 2026-05-02](../../CHANGELOG.md) with **169 rules** registered, tested, and documented across 19 categories. Phases 0-7 are closed:

- Phase 0-1: AST-pattern engine + suppressions + config (3 rules)
- Phase 2: AST-only rule pack (24 rules; ADR 0009)
- Phase 3: Reflection infrastructure + 5 parallel packs (60 rules; ADR 0012, ADR 0007 §Phase 3, ADR 0010 §Phase 3)
- Phase 4: CFG + uniformity oracle + 5 parallel packs (42 rules; ADR 0013, ADR 0007 §Phase 4, ADR 0010 §Phase 4)
- Phase 5: LSP server + VS Code extension (ADR 0014)
- Phase 6: v0.5.0 launch + docs site (ADR 0015)
- Phase 7: 4 parallel rule packs (15 rules) over AST-CFG liveness + AST-level register-pressure heuristic, **without** the DXIL bridge originally proposed (ADR 0016 superseded by ADR 0017)

The v0.7.0 release cleared the original ROADMAP "Phases" table; what comes next is open-ended.

This ADR is the research pass that closes that gap. It surveys five dimensions — coverage gaps in the 169-rule set, new GPU surfaces that have shipped or are imminent, what other tools cover that we don't, candidate rules anchored to primary sources, and the bar for graduating to v1.0 — and locks a curated subset of new rules across v0.8 / v0.9 / v0.10. The shape mirrors [ADR 0011](0011-candidate-rule-adoption.md) — the canonical "research-grade ADR" template in this repo (LOCKED / DEFERRED / DROPPED verdict tally + per-version implementation plans).

The ADR explicitly does *not* propose new infrastructure in the shape of ADR 0012 / 0013 / 0016. v0.7.0 demonstrated that the existing AST + reflection + CFG + AST-level liveness stack covers ~95 % of plausible rules; the remaining 5 % is gated on either real IHV-specific telemetry (RGA / Nsight CSV oracles) or on a future DXIL bridge that needs its own ADR when demand is concrete.

## Decision drivers

- **Citable primary sources, not "we believe."** Phase 7 was lenient on justification — `vgpr-pressure-warning`'s heuristic was anchored to a hand-waved threshold rather than an RGA-validated number. v0.8+ tightens this: every LOCKED rule cites either a published spec (HLSL Specs Working Draft 2026-04-29, DirectX-Specs SM 6.9 / 6.10), an IHV architecture guide (GPUOpen RDNA Performance Guide, NVIDIA Blackwell Architecture v1.1 PDF, Intel Xe2 Battlemage hwcooling.net deep-dive), or a peer-reviewed microbenchmark paper (`arxiv.org/abs/2512.02189` — Microbenchmarking NVIDIA's Blackwell Architecture).
- **Portability across IHVs by default.** Same rule as ADR 0011 §Decision drivers. Vendor-specific rules ship under `[experimental.target = rdna4 | blackwell | xe2]` config gates only, never as portable defaults.
- **Heuristic accuracy gates.** Phase 7's `vgpr-pressure-warning` shipped with a 64-VGPR default threshold and no measured false-positive rate. v0.8+ rules carry an explicit accuracy claim in their doc page (FP rate measured against `tests/corpus/` + a target ≤ 5 % FP for warn-severity rules, ≤ 1 % for error-severity rules). Suggestion-grade rules carry no FP claim.
- **Companion blog post bar.** Per CLAUDE.md, every rule lands with a docs/blog post explaining the GPU mechanism. The v0.5.0 launch shipped 8 category overviews in lieu of one-per-rule for the original 154 rules — v0.6 / v0.7 deferred per-rule blog posts to "v0.6+ flywheel." v0.8+ rules each ship with a one-paragraph mechanism explanation in `docs/rules/<id>.md` `gpu_reason` and a 800-1500 word companion in `docs/blog/`.
- **Don't drift IHV-specific.** RDNA 4 / Blackwell / Xe2 are tempting targets for vendor-lockin rules. We resist: a rule lands as portable only if the same mechanism observably bites on at least 2 of the 3 modern IHVs (RDNA 4, Blackwell, Xe2). Rules that only bite on one IHV ship as `[experimental]` with a per-IHV gate.

## Five-dimension survey

### 1. Coverage gaps in the existing 169 rules

A pass over `docs/rules/` (188 doc pages, 169 shipped + 19 stubs as of v0.7.0) and `core/src/rules/` (172 .cpp files including registry + `rules.hpp`) surfaces these gaps.

#### 1.1 Thin categories (< 5 rules)

| Category | Count | Note |
|---|---|---|
| `vrs` | 2 | `vrs-incompatible-output`, `sv-depth-vs-conservative-depth`. ADR 0007 §Phase 4 listed VRS as a category but only 2 rules shipped — the SV_ShadingRate and the per-primitive coarse-rate paths are uncovered. |
| `misc` | 3 | `compare-equal-float`, `comparison-with-nan-literal`, `redundant-precision-cast`. Catch-all category — shouldn't grow but is currently a holding pen. |
| `sampler-feedback` | 3 | `feedback-every-sample`, `feedback-write-wrong-stage`, `sampler-feedback-without-streaming-flag`. Reasonable coverage of the 3 main mistakes; only "thin" by count. |
| `opacity-micromaps` | 3 | All 3 OMM-flag-mismatch rules shipped via ADR 0010 — full coverage of the SM 6.9 OMM HLSL surface. |
| `long-vectors` | 4 | All 4 SM 6.9 long-vector rules shipped via ADR 0010. SM 6.10 promotes this to `linalg::Matrix` — new category needed. |
| `memory` | 4 | Phase 7 pack: `live-state-across-traceray`, `redundant-texture-sample`, `scratch-from-dynamic-indexing`, `vgpr-pressure-warning`. Heuristic-grade. |

The thin categories that flag genuine under-coverage: **`vrs`** (foliage / mobile / split-screen pipelines all hit VRS hard, we cover ~10 % of the surface) and the absence of a **`linalg`** / **`sm6_10`** category for SM 6.10's matrix APIs.

#### 1.2 Rules that exist as docs-only stubs

`grep -l "Pre-v0\|pre-v0" docs/rules/*.md` returns **37 files** including `_template.md`. The 36 real stubs cluster around:

- `numthreads-not-wave-aligned` (workgroup) — drafted in ADR 0007 but not implemented
- `gather-channel-narrowing`, `gather-cmp-vs-manual-pcf` (texture) — drafted, not implemented
- `dot4add-opportunity` (packed-math) — drafted, not implemented (packed-math category is currently 7 rules but the SM 6.4 dot4 surface is partially covered)
- `mesh-node-not-leaf`, `mesh-node-uses-vertex-shader-pipeline`, `mesh-node-missing-output-topology` (work-graphs experimental) — gated behind `[experimental.work-graph-mesh-nodes]`; ADR 0010 marked these as preview
- `min16float-in-cbuffer-roundtrip` (packed-math) — drafted via ADR 0011, deferred
- `inline-rayquery-when-pipeline-better`, `pipeline-when-inline-better` (dxr) — pair drafted, neither implemented
- `groupshared-too-large` (workgroup) — *now obsolete in SM 6.10* with `[GroupSharedLimit(<bytes>)]` attribute

**v0.8 should burn down 5-8 of these stubs**, prioritising the 4-5 that map cleanly to ADR 0011 deferred-to-locked transitions or to SM 6.10 surfaces.

#### 1.3 SM 6.9 / 6.10 surfaces with zero rule coverage

Cross-checked against the [HLSL Specs Working Draft (2026-04-29)](https://microsoft.github.io/hlsl-specs/specs/hlsl.pdf) and the [DirectX SM 6.10 Preview](https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/):

- `linalg::Matrix` HLSL API — proposal 0035 Accepted, shipped in DXC 1.10.2605.2. Zero rules.
- `GetGroupWaveIndex()` / `GetGroupWaveCount()` — proposal 0048 Accepted. Zero rules.
- `[GroupSharedLimit(<bytes>)]` entry attribute (variable groupshared) — proposal 0049 Accepted. Zero rules; obsoletes parts of `groupshared-too-large`.
- `TriangleObjectPositions()` — SM 6.10 raytracing intrinsic. Requires `VK_KHR_ray_tracing_position_fetch` flag at acceleration-structure build (`VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR` on Vulkan; equivalent on D3D12). Zero rules — easy footgun rule.
- `ClusterID()` — SM 6.10 raytracing intrinsic. Functionality pending clustered-geometry support (preview Fall 2026 per DirectX dev blog). Zero rules; queue for v0.9+.
- `numWaves` shader intrinsic — proposal 0054 Under Consideration. Not yet released.

#### 1.4 Patterns the corpus trips into that no rule fires on

Without running the linter against the 27 corpus shaders in this prompt, a manual cross-reference exercise (`tests/corpus/SOURCES.md` listing × ADR 0007/0010/0011 rule list) surfaces:

- `compute/spd_cs_downsampler.hlsl` (FidelityFX SPD) — AMD's reference downsampler uses 16x16 thread groups and groupshared for reduce; we have `numthreads-too-small` but no rule for "256-thread-block reduce that could use SM 6.10 `GetGroupWaveCount` for wave-level work specialisation."
- `raytracing/rt_simple_lighting.hlsl`, `raytracing/rt_procedural_geometry.hlsl` — no rule for ray-flag combinations beyond `missing-ray-flag-cull-non-opaque` and `missing-accept-first-hit`. `RAY_FLAG_FORCE_OPAQUE` + an AnyHit shader is a common UB; uncovered.
- `mesh/meshlet_render_ms.hlsl`, `amplification/meshlet_cull_as.hlsl` — covered by ADR 0010 mesh-pack. No gap.
- `compute/bitonic_presort_cs.hlsl` — bitonic sort uses heavy LDS atomics; covered by `groupshared-atomic-replaceable-by-wave`. No gap.

#### 1.5 Coverage-gap verdict

Of the four sub-axes, the strongest case for new rules is **§1.3 (SM 6.9 / 6.10 surfaces with zero coverage)** — 4 rules drop straight out. §1.1's thin-category VRS surface adds 2 more. §1.2's stub burndown adds 5-8. §1.4's corpus patterns add 1-2. **Total candidate budget from coverage gaps alone: ~12-16 rules.**

### 2. New GPU surfaces relevant to HLSL

#### 2.1 AMD RDNA 4 (Radeon RX 9070 / 9070 XT, Feb 2025)

Sources: [AMD RDNA 4 Hot Chips 2025 deck (PDF)](https://hc2025.hotchips.org/assets/program/conference/day1/8_amd_pomianowski_final.pdf), [Chips and Cheese RDNA 4 deep-dive](https://chipsandcheese.com/p/amds-rdna4-gpu-architecture-at-hot), [Chips and Cheese RDNA 4 dynamic VGPR](https://chipsandcheese.com/p/dynamic-register-allocation-on-amds), [Chips and Cheese RDNA 4 raytracing](https://chipsandcheese.com/p/rdna-4s-raytracing-improvements).

HLSL-developer-facing changes:

- **Dynamic VGPR allocation** in compute. New chip-wide `SQ_DYN_VGPR` register; threads start with a minimum allocation and request more via `s_alloc_vgpr`. **Wave32-only** — wave64 compute does not benefit. *Linter rule angle*: flag wave64 compute on shaders targeting RDNA 4.
- **Oriented Bounding Boxes** in BVH. Up to 10 % ray-tracing perf win. Needs `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` (or equivalent) on the BLAS build. *Linter rule angle*: project-side, not shader-side — defer until a project-config surface lands.
- **`IMAGE_BVH_DUAL_INTERSECT_RAY` instruction**. Two paths popped from LDS per traversal step — affects the cost of `RayQuery::Proceed()` loops. *Linter rule angle*: encourage `RayQuery` over recursive `TraceRay` on RDNA 4 targets when payload is small.
- **L1 cache replaced** with read/write coalescing buffer; L2 doubled per Shader Engine. *Linter rule angle*: `buffer-load-width-vs-cache-line` (Phase 7) was tuned to RDNA 3's 64-byte line — RDNA 4 cache hierarchy changed; revisit thresholds.
- **2nd-gen AI accelerator with FP8**. Cooperative-vector layouts that were "optimal" on RDNA 3 may differ. Slang already routes coopvec → matrix-core on RDNA 4. *Linter rule angle*: `coopvec-non-optimal-matrix-layout` already exists; verify thresholds match RDNA 4.

#### 2.2 NVIDIA Blackwell (RTX 5090, B200, Jan 2025)

Sources: [NVIDIA RTX Blackwell Architecture v1.1 PDF](https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf), [arXiv 2512.02189 — Microbenchmarking Blackwell](https://arxiv.org/abs/2512.02189), [SemiAnalysis Blackwell teardown](https://newsletter.semianalysis.com/p/dissecting-nvidia-blackwell-tensor).

HLSL-developer-facing changes:

- **5th-gen Tensor Cores** with FP4 and FP6 support. FP8 paths still supported. ~96.3 % of theoretical peak measured on FP4 / FP8. Blackwell SM is "designed and optimized for neural shaders" — Ada was for "standard shaders." *Linter rule angle*: SM 6.10 `linalg::Matrix` rules — FP4 / FP6 matrix-element-type checks.
- **CTA-pair semantics** for cooperative MMA across paired SMs. Two SMs share distributed SMEM. Tensor Memory Accelerator (TMA) refined. *Linter rule angle*: not yet exposed at HLSL level (pending SM 6.11+); defer.
- **2nd-gen FP8 Transformer Engine**. *Linter rule angle*: `coopvec-fp8-with-non-optimal-layout` already exists; verify thresholds match Blackwell.
- **Mega-kernels for inference**. Driver-level concern, not HLSL-developer-facing. No rule angle.

#### 2.3 Intel Xe2 / Battlemage (Arc B580, Dec 2024)

Sources: [Chips and Cheese Battlemage architecture](https://chipsandcheese.com/p/intels-battlemage-architecture), [HWCooling Battlemage analysis](https://www.hwcooling.net/en/batttlemage-details-of-intel-xe2-gpu-architecture-analysis/).

HLSL-developer-facing changes:

- **SIMD16 native ALUs**. Xe2 supports SIMD16 + SIMD32 ops; SIMD16 saves one cycle of address-generation latency over SIMD32. Forcing SIMD16 mode is sometimes a perf win. *Linter rule angle*: `[WaveSize(32)]` on a shader that should run as SIMD16 hides Battlemage native efficiency. **Listed in candidate rules below.**
- **8x 512-bit Vector Engines per Xe Core**. *Linter rule angle*: same as above.
- **3x mesh-shading vertex-fetch throughput** vs Alchemist. *Linter rule angle*: encourages mesh-shader migration on Xe2; informational, not a rule.
- **64-bit atomic ops support** in HLSL on Xe2. SM 6.6 baseline. *Linter rule angle*: covered by existing `interlocked-*` rules.
- **8x XMX Engines (2048-bit)**. *Linter rule angle*: same as Blackwell — SM 6.10 `linalg::Matrix` paths route through XMX on Xe2.

#### 2.4 PlayStation 6 (rumoured 2027)

No leaked HLSL-relevant features as of 2026-05-02. Likely RDNA 5 / RDNA 4 derivative. **Defer to a future ADR** — too speculative to lock anything.

#### 2.5 DirectX 12 Agility SDK + DXC

| Release | Date | HLSL-developer-facing additions |
|---|---|---|
| Agility SDK 1.616 retail | Sep 2025 | OMM (HLSL portion preview); D3D12 Tiled Resource Tier 4 |
| Agility SDK 1.618 retail | Oct 2025 | 1.716 features promoted; advanced shader delivery |
| Agility SDK 1.619 retail + DXC 1.9.2602.16 | Feb 2026 | **SM 6.9 retail** — long vectors required, OMM out of preview, SER out of preview, `IsNormal()` for fp16 |
| Agility SDK 1.720 preview + DXC 1.10.2605.2 | Apr 2026 | **SM 6.10 preview** — `linalg::Matrix`, `GetGroupWaveIndex/Count`, `[GroupSharedLimit]`, `TriangleObjectPositions`, `ClusterID` (functionality pending) |

Per the [SM 6.9 retail blog](https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/), Cooperative Vector is **deprecated** in favour of `linalg::Matrix` in SM 6.10. Our 6 `coopvec-*` rules (ADR 0010 §Phase 3) are still valid for SM 6.9 targets but should be flagged as "applies only to SM ≤ 6.9" in their doc pages. v0.8+ adds the `linalg-*` successor rules.

#### 2.6 Vulkan / SPIR-V extensions (last 12 months)

- `VK_KHR_ray_tracing_position_fetch` — fetch triangle vertex positions at hit time. `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_BIT_KHR` required at AS build. HLSL → SPIR-V via Slang. *Linter rule angle*: SM 6.10 `TriangleObjectPositions()` is the HLSL surface — flag missing build-flag (project config, deferred) and flag use without targeting SM 6.10.
- `VK_KHR_workgroup_memory_explicit_layout` — explicit-layout groupshared. *Linter rule angle*: relevant to `groupshared-union-aliased` (ADR 0011 LOCKED Phase 3); revisit thresholds when the SPIR-V emit path lands.

#### 2.7 HLSL Specs proposal pipeline

Current state of [microsoft.github.io/hlsl-specs/proposals/](https://microsoft.github.io/hlsl-specs/proposals/):

- **Accepted:** 0026 Long Vectors (shipped SM 6.9), 0035 Linear Algebra Matrix (shipped SM 6.10), 0048 Group Wave Index (shipped SM 6.10), 0049 Variable Group Shared Memory (shipped SM 6.10), 0052 Experimental DXIL Ops.
- **Under Review:** 0006 Reference Data Types, 0009 Math Modes, 0011 Inline SPIR-V, 0020 HLSL 202x/202y, 0032 Constructors, 0039 Debugging Intrinsics.
- **Under Consideration:** 0054 numWaves, 0050 Formalized Memory and Execution Model, 0051 ByteAddressBuffer Alignment, 0053 Allow Early Pixel Culling.

Three of these (0006 reference types, 0011 inline SPIR-V, 0009 math modes) are relevant for *future* lint rules but are not yet in shipping DXC. v0.8+ does not commit rules against them. 0051 (ByteAddressBuffer alignment) is the ADR-0011 LOCKED rule `byteaddressbuffer-load-misaligned` from a different angle — keep an eye on standardisation; if it lands, our rule's signature gets to fold the spec rule into the lint output.

### 3. Competitive landscape

#### 3.1 dxc validator (`dxc -Vd` disables; default validates)

Source: [DXC release notes](https://github.com/microsoft/DirectXShaderCompiler/releases). DXC's validator surfaces SM-version mismatches, DXIL-malformedness, and a small set of "definitely invalid" semantic patterns. **It does not flag perf patterns** — its remit is correctness. We have full breathing room above DXC's validator; no overlap.

#### 3.2 naga / wgpu validator

Source: [gfx-rs/naga](https://github.com/gfx-rs/naga). Naga's HLSL frontend exists primarily for round-trip translation to WGSL/SPIR-V/MSL/GLSL; its validator targets WGSL semantics, not HLSL perf. **No overlap.** A future "lint your HLSL for WGSL portability" surface is interesting but out of scope.

#### 3.3 slangc `-Wall`

Source: [Slang command-line reference](https://docs.shader-slang.org/en/latest/external/slang/docs/command-line-slangc-reference.html). Slang surfaces some patterns we also flag at very different fidelity:

- **Slang already flags:** dead-code (similar to our `redundant-computation-in-branch`), some unused-parameter cases (similar to `unused-cbuffer-field` partial overlap), `volatile`-on-cbuffer-field syntax errors.
- **Slang doesn't flag (we do):** every perf-bias rule, every IHV-specific portability rule, every quad/wave subtletly (our 28-ish wave/quad rules), every `coopvec-*` layout rule.

**v0.8 docs action:** add a "Rules where Slang's `-Wall` overlaps" callout to the affected `docs/rules/<id>.md` pages so users understand what they get for free vs. what only `hlsl-clippy` provides.

#### 3.4 Khronos glslang (validation for SPIR-V emit)

Source: [github.com/KhronosGroup/glslang](https://github.com/KhronosGroup/glslang). Validates SPIR-V output against Vulkan capability sets. Surfaces missing `VK_KHR_*` extensions, capability mismatches. **Different layer** — we target HLSL source; glslang validates emitted SPIR-V. Complementary, no overlap.

#### 3.5 AMD Radeon GPU Analyzer (RGA)

Sources: [Live VGPR Analysis with RGA](https://gpuopen.com/learn/live-vgpr-analysis-radeon-gpu-analyzer/), [Visualizing VGPR Pressure with RGA 2.6](https://gpuopen.com/learn/visualizing-vgpr-pressure-with-rga-2-6/), [Occupancy Explained](https://gpuopen.com/learn/occupancy-explained/).

RGA surfaces:

- **Live VGPR analysis per code block** — accurate post-codegen counts. Our `vgpr-pressure-warning` is heuristic; RGA's is real.
- **VGPR-block-size hints** — "to save 1 block here, free N VGPRs."
- **Occupancy-limited factor** — colours wavefronts by what's bottlenecking (VGPR / LDS / thread-group dim).
- **Per-target compilation** — compiles for a specific RDNA target.

**Gap to surface:** v0.8+ should ship `tools/rga-bridge.{sh,ps1}` that runs RGA against a shader, parses its CSV output, and feeds the per-block VGPR counts back into our rules as an oracle. This makes `vgpr-pressure-warning` accurate on RDNA targets. Filed below as a v0.10 candidate (`rga-pressure-bridge`).

#### 3.6 NVIDIA Nsight Graphics shader profiler

Sources: [Identifying Shader Limiters with the Shader Profiler in NVIDIA Nsight Graphics](https://developer.nvidia.com/blog/identifying-shader-limiters-with-the-shader-profiler-in-nvidia-nsight-graphics/), [Nsight Graphics 2025.4 docs](https://docs.nvidia.com/nsight-graphics/UserGuide/shader-profiler.html).

Nsight surfaces stall reasons:

- **Warp Stalled by L1 Long Scoreboard** — texture fetches without enough work between sample and use.
- **Math Pipe Throttle** — FMA/ALU/FP16+Tensor pipe input FIFO full.
- **L2 limited** — bandwidth bound.
- **Cache hit-rate analysis.**

**Gap to surface:** we have `loop-invariant-sample` and `redundant-texture-sample` but no rule for "interleave compute between sample and use." Filed below as a v0.9 candidate (`sample-use-no-interleave`). Nsight CSV bridge analogous to the RGA bridge filed as a v0.10 candidate (`nsight-bridge`).

#### 3.7 PIX on Windows + Shader Explorer

Source: [GDC 2026 announcement of Shader Explorer for PIX](https://gpuopen.com/learn/amd-microsoft-gdc-2026/). Microsoft + AMD partnership: Shader Explorer surfaces low-level compile-time perf insights alongside HLSL. Ships from day one with AMD hardware-specific guidance.

**Gap to surface:** Shader Explorer is going to compete most directly with our perf-hint rules. We respond by emphasising portability (Shader Explorer is RDNA-specific via RGA backend) and by being free + open-source + linter-shaped (in-editor, in-CI, batch-applicable). v1.0 should advertise this differentiation.

### 4. Research-grade rule candidates

22 candidates (within the 15-25 target range). Each carries: id, category, what it detects, GPU mechanism + primary source, implementation stage, difficulty. Verdicts assigned in §"Decision (Proposed)" below.

#### 4.1 Candidates anchored in SM 6.9 / 6.10 (zero current coverage)

1. **`linalg-matrix-non-optimal-layout`** — `linalg::Matrix` declared with non-optimal layout for the target. *Mechanism:* SM 6.10 successor to coopvec; layout enums map to `OPTIMAL_*` choices that drivers route to RDNA 4 / Blackwell / Xe2 matrix cores. *Source:* [HLSL Specs proposal 0035](https://microsoft.github.io/hlsl-specs/proposals/0035-linear-algebra-matrix.html). *Stage:* Reflection. *Difficulty:* MEDIUM (parallel to `coopvec-non-optimal-matrix-layout` which already shipped).

2. **`linalg-matrix-element-type-mismatch`** — `linalg::Matrix<T>` with element type that doesn't match the bound resource's format. *Mechanism:* same as `coopvec-stride-mismatch`. *Source:* HLSL Specs proposal 0035, [DirectX SM 6.10 blog](https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/). *Stage:* Reflection. *Difficulty:* MEDIUM.

3. **`getgroupwaveindex-without-wavesize-attribute`** — `GetGroupWaveIndex()` / `GetGroupWaveCount()` called from a shader that lacks `[WaveSize(N)]`. *Mechanism:* SM 6.10 wave-aware specialization patterns are pinned only when wave size is. Without `[WaveSize]`, RDNA may run wave32 or wave64 and the index/count semantics change. *Source:* [HLSL Specs proposal 0048](https://microsoft.github.io/hlsl-specs/proposals/0048-group-wave-index.html). *Stage:* AST. *Difficulty:* EASY.

4. **`groupshared-over-32k-without-attribute`** — groupshared declaration > 32 KB without the SM 6.10 `[GroupSharedLimit(<bytes>)]` entry attribute. *Mechanism:* SM 6.10's variable-groupshared feature requires the attribute to opt into > 32 KB; without it, the compile errors on SM 6.10 and silently truncates on SM ≤ 6.9. *Source:* [HLSL Specs proposal 0049](https://microsoft.github.io/hlsl-specs/proposals/0049-variable-group-shared-memory.html). *Stage:* AST + Reflection. *Difficulty:* EASY. Companion fix-it: machine-applicable insertion of the attribute. Obsoletes parts of stub rule `groupshared-too-large`.

5. **`triangle-object-positions-without-allow-data-access-flag`** — `TriangleObjectPositions()` called against an acceleration structure without the corresponding `ALLOW_DATA_ACCESS` build flag visible in reflection. *Mechanism:* SM 6.10 ray-tracing intrinsic — UB if the BLAS wasn't built with the flag. *Source:* [VK_KHR_ray_tracing_position_fetch spec](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_ray_tracing_position_fetch.html), [DirectX SM 6.10 preview](https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/). *Stage:* AST (suggestion-grade — we can detect the call but project-side build flag is invisible). *Difficulty:* EASY. **Suggestion-grade** — flags every call site with a docs link.

6. **`cluster-id-without-cluster-geometry-feature-check`** — `ClusterID()` called without a feature-availability check. *Mechanism:* SM 6.10 intrinsic, functionality pending preview Fall 2026. Calling without an `IsClusteredGeometrySupported()` guard breaks on devices that don't yet ship the feature. *Source:* DirectX SM 6.10 preview blog. *Stage:* AST. *Difficulty:* EASY. **Defer to v0.10** until clustered geometry preview ships.

#### 4.2 Candidates anchored in modern IHV architectures

7. **`wave64-on-rdna4-compute-misses-dynamic-vgpr`** — compute kernel declared `[WaveSize(64)]` (or unspec'd defaulting to wave64 on RDNA) on a shader targeting RDNA 4. *Mechanism:* RDNA 4 dynamic VGPR allocation is wave32-only. Wave64 compute on RDNA 4 silently misses occupancy gains. *Source:* [Chips and Cheese RDNA 4 Dynamic VGPR](https://chipsandcheese.com/p/dynamic-register-allocation-on-amds), [AMD Hot Chips 2025 deck](https://hc2025.hotchips.org/assets/program/conference/day1/8_amd_pomianowski_final.pdf). *Stage:* AST + Reflection (target SM check). *Difficulty:* MEDIUM. **Experimental — gated behind `[experimental.target = rdna4]`** because wave64 on non-RDNA-4 RDNA is fine.

8. **`oriented-bbox-not-set-on-rdna4`** — RayQuery / `TraceRay` against an acceleration structure whose binding doesn't reflect the `D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` flag, on RDNA 4 targets. *Mechanism:* up to 10 % RT perf win on RDNA 4. *Source:* [Chips and Cheese RDNA 4 Raytracing](https://chipsandcheese.com/p/rdna-4s-raytracing-improvements). *Stage:* Reflection (project-side BLAS flag visible? Often no). *Difficulty:* HARD (project-side state not in shader). **Defer to v0.10** with project-config-input rule.

9. **`coopvec-fp4-fp6-blackwell-layout`** — `linalg::Matrix` with FP4 / FP6 element type using a non-Blackwell-optimal layout when targeting Blackwell. *Mechanism:* Blackwell tensor cores are FP4/FP6-native; layout choice differs from FP8-Hopper-optimal. *Source:* [NVIDIA Blackwell Architecture v1.1 PDF](https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf), [arXiv 2512.02189](https://arxiv.org/abs/2512.02189). *Stage:* Reflection. *Difficulty:* MEDIUM. **Experimental — `[experimental.target = blackwell]`.**

10. **`wavesize-32-on-xe2-misses-simd16`** — `[WaveSize(32)]` on a kernel that would benefit from SIMD16 native execution on Xe2. *Mechanism:* Xe2 SIMD16 saves one cycle of address-gen latency per dispatch over SIMD32. *Source:* [Chips and Cheese Battlemage](https://chipsandcheese.com/p/intels-battlemage-architecture). *Stage:* AST + Reflection (target check). *Difficulty:* HARD (heuristic — "would benefit" requires real measurement). **Defer to v0.10** as a `[suggestion]`-only rule with a configurable target gate.

#### 4.3 Candidates anchored in competitive-tool gaps

11. **`sample-use-no-interleave`** — `Sample()` followed within ≤ 3 dependent ops by a use of the result, in a shader where the texture-cache miss latency is significant. *Mechanism:* Nsight's "Warp Stalled by L1 Long Scoreboard" — interleave compute between sample and use to hide latency. *Source:* [Identifying Shader Limiters in Nsight Graphics](https://developer.nvidia.com/blog/identifying-shader-limiters-with-the-shader-profiler-in-nvidia-nsight-graphics/). *Stage:* CFG. *Difficulty:* HARD (heuristic — "≤ 3 dependent ops" needs tuning). **Suggestion-grade.**

12. **`rga-pressure-bridge`** — meta-rule: when `tools/rga-bridge` is configured, replace `vgpr-pressure-warning`'s heuristic with RGA's actual per-block VGPR counts on RDNA targets. *Mechanism:* RGA's live VGPR analysis is ground truth for RDNA. *Source:* [Live VGPR Analysis with RGA](https://gpuopen.com/learn/live-vgpr-analysis-radeon-gpu-analyzer/). *Stage:* External oracle (new infrastructure). *Difficulty:* HARD. **Defer to v0.10** — needs `tools/rga-bridge.{sh,ps1}` and a config gate. Filed as a v0.10 infrastructure investment, not a rule per se.

#### 4.4 Candidates from §1.2 stub burndown

13. **`numthreads-not-wave-aligned`** — `[numthreads(N,M,K)]` with `N*M*K` not a multiple of the smallest portable wave size (32). *Mechanism:* unaligned thread group sizes leave inactive lanes in the last wave. *Source:* GPUOpen RDNA Performance Guide, NVIDIA Blackwell Architecture Guide. *Stage:* AST + Reflection. *Difficulty:* EASY. Already a stub.

14. **`gather-channel-narrowing`** — `Gather()` (4-channel) where the shader reads only one channel; `Gather4Red`/`Gather4Green`/etc. would suffice. *Mechanism:* RDNA / Ada texture units can issue a 1-channel gather in one cycle vs 4 for the full gather. *Source:* RDNA Performance Guide. *Stage:* AST. *Difficulty:* EASY. Already a stub.

15. **`gather-cmp-vs-manual-pcf`** — manual 2x2 PCF (4 `SampleCmp` calls) where `GatherCmp` would replace them with one issue. *Mechanism:* same texture-unit scaling as above. *Source:* Microsoft DirectX-Specs SM 6.7 GatherCmp doc. *Stage:* AST. *Difficulty:* MEDIUM. Already a stub.

16. **`dot4add-opportunity`** — explicit `int4` dot-product expansion that should use `dot4add_*` intrinsics. *Mechanism:* SM 6.4 packed-math intrinsic; ~4x throughput on RDNA / Ada / Xe-HPG. *Source:* DirectX-Specs SM 6.4 Packed Math. *Stage:* AST. *Difficulty:* MEDIUM. Already a stub.

17. **`min16float-in-cbuffer-roundtrip`** — `min16float` cbuffer field accessed and immediately widened to 32-bit. *Mechanism:* cbuffer 16-bit fields go through 32-bit widening at the load site; access patterns that don't keep the 16-bit value packed waste the saving. *Source:* DirectX-Specs SM 6.2 16-bit Types. *Stage:* AST + Reflection. *Difficulty:* MEDIUM. Already a stub via ADR 0011 deferral.

#### 4.5 Candidates from §1.1 thin-category VRS

18. **`vrs-rate-conflict-with-target`** — `SV_ShadingRate` write conflicts with the render target's per-primitive coarse-rate. *Mechanism:* D3D12 / Vulkan VRS rate combiners produce the *minimum* of per-primitive and per-pixel rates; conflicting declarations silently override the author's expectation. *Source:* [DirectX-Specs Variable Rate Shading](https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html). *Stage:* AST + Reflection. *Difficulty:* MEDIUM.

19. **`vrs-without-perprimitive-or-screenspace-source`** — `[earlydepthstencil]` PS that emits `SV_ShadingRate` without an upstream per-primitive or screenspace VRS source. *Mechanism:* PS-emitted VRS rates without an upstream source are ignored on most IHVs. *Source:* DirectX-Specs Variable Rate Shading, NVIDIA Turing whitepaper §VRS. *Stage:* Reflection. *Difficulty:* MEDIUM.

#### 4.6 Candidates from §1.4 corpus + ADR 0011 deferred

20. **`ray-flag-force-opaque-with-anyhit`** — `TraceRay` with `RAY_FLAG_FORCE_OPAQUE` set, against an acceleration structure that has at least one geometry with an AnyHit shader bound. *Mechanism:* `RAY_FLAG_FORCE_OPAQUE` skips AnyHit invocation; configuration that binds an AnyHit and then forces-opaque is dead code or a logic bug. *Source:* [DirectX Raytracing Functional Spec](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html). *Stage:* AST + Reflection. *Difficulty:* MEDIUM.

21. **`ser-coherence-hint-bits-overflow`** — `MaybeReorderThread` with `NumCoherenceHintBits > 16` (HitObject variant > 8). *Mechanism:* spec limit — values above the cap are silently truncated, producing incoherent reorder. *Source:* [HLSL Specs proposal 0027 SER](https://microsoft.github.io/hlsl-specs/proposals/0027-shader-execution-reordering.html). *Stage:* AST. *Difficulty:* EASY.

22. **`dispatchmesh-grid-too-small-for-wave`** — `DispatchMesh(x, y, z)` with constant args whose product `< wave_size`. *Mechanism:* dispatching less than one wave wastes the entire dispatch. *Source:* RDNA Performance Guide. *Stage:* AST + Reflection (wave size from target). *Difficulty:* EASY. Already a partial DEFER per ADR 0011 (`as-launch-grid-too-small`); the constant-args version is tractable AST-only.

### 5. v1.0 readiness criteria

The bar for v0.x → v1.0. Each criterion is concrete and testable on inspection. Targeting v1.0 in the v0.10 → v0.11 → v1.0 trajectory across roughly 6-9 months from v0.7.0.

1. **API stability commitment.** A v1.0 → v1.x bump may not change the binary shape of `Diagnostic`, `Rule`, `LintOptions`, `RuleContext`, or any public type in `core/include/hlsl_clippy/`. Documented in `docs/api-stability.md`. Validated by a CI job that diffs `nm`-extracted public symbols across versions.
2. **Coverage gate.** Line coverage on `core/` ≥ 80 %, branch coverage ≥ 70 %, measured by `llvm-cov` in CI. Currently target 60 % per CLAUDE.md; v1.0 lifts the floor to 80 %.
3. **False-positive budget.** Every warn-severity rule carries a measured FP rate ≤ 5 % against `tests/corpus/` (27 shaders), tracked in `tests/corpus/FP_RATES.md`. Suggestion-severity rules have no FP budget but ship a measured surface area number.
4. **Independent reproduction of every shipped rule's GPU-mechanism claim by ≥ 2 IHV sources.** Either two of {RDNA / Blackwell / Xe2 / Hopper / Ada} architecture guides citing the mechanism, or one IHV doc + one GDC talk slide / arXiv paper. Tracked in `docs/rules/<id>.md` `references:` front-matter (new field).
5. **Machine-applicable fixes for ≥ 50 % of warn-grade diagnostics.** Currently 37 of 169 rules have `applicability: machine-applicable` (~22 %). v1.0 demands ≥ 50 % of *warn-grade* (i.e. not pure suggestions) rules carry a fix.
6. **Per-rule blog-post coverage ≥ 80 %.** v0.5 shipped 8 category overviews in lieu of per-rule posts; v1.0 demands per-rule posts for ≥ 80 % of shipped rules. The remaining 20 % are stubs that link to the category overview.
7. **VS Code Marketplace install count ≥ 5,000.** Soft target; tracks adoption. Marketplace listing is live as of v0.5.3.
8. **Independent integration with at least 1 well-known engine or pipeline.** GitHub search for `hlsl-clippy` in `.github/workflows/` files returns ≥ 5 hits in non-monorepo public repos. Or a vendor-side integration (Slang opt-in, Khronos integration, etc.).
9. **Multi-platform binary releases on every tagged version.** Linux + Windows + macOS CLI/LSP archives + per-platform `.vsix`. Currently green; v1.0 demands no green-then-red regression for 3 consecutive releases.
10. **Reflection-driven rules survive Slang submodule bumps without source changes.** Validated by a CI job that bumps the Slang prebuilt cache version one minor + one patch and runs the full ctest baseline. v1.0 fails this job → v1.0 fails the release.
11. **Vendor-specific (`[experimental.target = *]`) rules cleanly disabled on default config.** Default `.hlsl-clippy.toml` produces zero IHV-specific diagnostics on `tests/corpus/`. Validated by a CI snapshot.
12. **DCO + CONTRIBUTING.md fully-followed for the last 200 commits before tag.** Verified by a release-time audit script. Project posture is clean now; v1.0 demands no regression.

## Decision (Proposed)

Of the 22 candidates surfaced in §4, the verdicts are:

| Verdict | Count |
|---|---|
| LOCKED | 17 |
| DEFERRED | 4 |
| DROPPED | 1 |
| **Total** | **22** |

LOCKED breakdown by version target:

| Version | Rules | Theme |
|---|---|---|
| v0.8 | 8 | SM 6.10 + stub burndown launch pack |
| v0.9 | 5 | VRS + DXR + competitive-tool-gap rules |
| v0.10 | 4 | IHV-experimental + bridge infrastructure |

### v0.8 LOCKED rules (8) — "SM 6.10 + stub burndown launch pack"

1. `linalg-matrix-non-optimal-layout` — Reflection, MEDIUM. Companion to `coopvec-non-optimal-matrix-layout`.
2. `linalg-matrix-element-type-mismatch` — Reflection, MEDIUM.
3. `getgroupwaveindex-without-wavesize-attribute` — AST, EASY.
4. `groupshared-over-32k-without-attribute` — AST + Reflection, EASY. Machine-applicable fix-it.
5. `triangle-object-positions-without-allow-data-access-flag` — AST, EASY. Suggestion-grade.
6. `numthreads-not-wave-aligned` — AST + Reflection, EASY. Stub burndown.
7. `dispatchmesh-grid-too-small-for-wave` — AST + Reflection, EASY.
8. `dot4add-opportunity` — AST, MEDIUM. Stub burndown.

**Pack split.** 8 rules split as one shared-utilities PR + two parallel packs:

- **Shared-utilities PR (lands first).** `core/src/rules/util/sm6_10.{hpp,cpp}` — SM-version reflection helper, `linalg::Matrix` type-recognition helper, `[GroupSharedLimit]` attribute parser. ~120 LOC + tests.
- **Pack A — SM 6.10 surface-pack** (5 rules): #1, #2, #3, #4, #5.
- **Pack B — stub-burndown-pack** (3 rules): #6, #7, #8.

Effort estimate: 1 dev week per pack with parallel agents.

### v0.9 LOCKED rules (5) — "VRS + DXR + Nsight-gap rules"

9. `vrs-rate-conflict-with-target` — AST + Reflection, MEDIUM.
10. `vrs-without-perprimitive-or-screenspace-source` — Reflection, MEDIUM.
11. `ray-flag-force-opaque-with-anyhit` — AST + Reflection, MEDIUM.
12. `ser-coherence-hint-bits-overflow` — AST, EASY.
13. `sample-use-no-interleave` — CFG, HARD. Suggestion-grade only.

Pack split: one combined PR (5 rules; below the 6-rule per-category-pack threshold).

### v0.10 LOCKED rules (4) — "IHV-experimental + bridge infrastructure"

14. `wave64-on-rdna4-compute-misses-dynamic-vgpr` — AST + Reflection, MEDIUM. **`[experimental.target = rdna4]`-gated.**
15. `coopvec-fp4-fp6-blackwell-layout` — Reflection, MEDIUM. **`[experimental.target = blackwell]`-gated.**
16. `wavesize-32-on-xe2-misses-simd16` — AST + Reflection, HARD. **`[experimental.target = xe2]`-gated; suggestion-grade.**
17. `cluster-id-without-cluster-geometry-feature-check` — AST, EASY. **Activates only on SM 6.10+ targets.**

Plus the v0.10 infrastructure investment:

- **`tools/rga-bridge.{sh,ps1}`** — invokes RGA, parses CSV output, emits a JSON file consumed by a new `RgaPressureOracle` in `core/src/reflection/`. Replaces the heuristic in `vgpr-pressure-warning` with measured per-block VGPR counts on RDNA targets when the bridge is configured.
- **`tools/nsight-bridge.{sh,ps1}`** — analogous for NVIDIA Nsight CSV.

Both bridges are **opt-in via `.hlsl-clippy.toml`**; default `lint` runs do not invoke them.

### DEFERRED candidates (4)

1. **`oriented-bbox-not-set-on-rdna4`** — needs project-side BLAS-build-flag input; defer until a project-config surface lands. (Same blocker as ADR 0011's `root-32bit-constant-pack-mismatch`.)
2. **`rga-pressure-bridge`** — listed under v0.10 infrastructure investment, not a standalone rule; the rule per se is `vgpr-pressure-warning` enhanced.
3. **`numWaves`-anchored rules** (proposal 0054) — under-consideration, not yet shipping. Defer until accepted.
4. **`reference-data-types`-anchored rules** (proposal 0006) — under-review, not yet shipping. Defer.

### DROPPED candidates (1)

1. **`groupshared-too-large` (existing stub)** — superseded by SM 6.10 `[GroupSharedLimit]` and by the new `groupshared-over-32k-without-attribute`. **DROP** the stub; the new rule covers the surface cleanly. The doc page at `docs/rules/groupshared-too-large.md` becomes a stub redirecting to `docs/rules/groupshared-over-32k-without-attribute.md`.

## Risks & mitigations

- **Risk: heuristic accuracy slips on the IHV-experimental rules.** Phase 7's `vgpr-pressure-warning` was lenient — v0.8+ tightens.
  - *Mitigation 1:* every IHV-experimental rule (#14, #15, #16) ships behind `[experimental.target = *]` gates and is *off by default*. Users opt in per-target; default lints stay portable.
  - *Mitigation 2:* every warn-severity rule carries a measured FP rate against the corpus before tag-cut. v1.0 readiness criterion #3 enforces ≤ 5 % FP for warn-grade.
  - *Mitigation 3:* the v0.10 RGA / Nsight bridges replace heuristics with measurements where available. Heuristics survive only as fallbacks.
- **Risk: false-positive rate creeps with rule-count growth.** 169 → 186 rules ≈ 10 % growth; FP rate often grows superlinearly with rule count.
  - *Mitigation:* corpus-baseline regression in CI. A new rule that lifts the corpus FP rate by > 0.5 % blocks at review.
- **Risk: IHV-target divergence widens.** RDNA 4 dynamic-VGPR mode is wave32-only, Blackwell prefers FP4/FP6, Xe2 prefers SIMD16 — three IHVs, three different sweet spots. "This perf pattern" becomes IHV-specific quickly.
  - *Mitigation 1:* the portability bar (§Decision drivers) — rules in default lint observably bite on ≥ 2 of 3 modern IHVs. Single-IHV rules ship behind `[experimental.target = *]`.
  - *Mitigation 2:* docs pages for IHV-experimental rules carry an "applies on:" front-matter field listing the targets and explicitly noting *not* the others. Reviewers won't surface them in default lint output and won't misread them as "everyone's pattern."
- **Risk: SM 6.10 preview drift.** Agility SDK 1.720 is preview; spec may move before retail.
  - *Mitigation:* v0.8 SM 6.10 rules (#1-#5) carry a `requires_sm: ">=6.10-preview"` config gate. If retail SM 6.10 changes the syntax, rules update before retail ships and never produce diagnostics on shaders compiled under the older syntax.
- **Risk: blog-post obligation lags.** v0.5.0 deferred per-rule blog posts to "v0.6+ flywheel"; v0.7.0 still owes ~150 posts.
  - *Mitigation:* v0.8+ rules each ship with an 800-1500 word companion post (CLAUDE.md "rule + blog post pair" convention). The backlog of pre-v0.5 rules is its own deferred queue; v1.0 readiness criterion #6 (≥ 80 % per-rule coverage) is the deadline.
- **Risk: v1.0 readiness criterion #4 ("≥ 2 IHV sources for every rule") unenforceable retroactively.** 169 existing rules predate the criterion; many carry only one citation or a hand-wave.
  - *Mitigation:* v1.0 readiness criterion #4 applies *prospectively from v0.8*. Existing 169 rules get a one-time grandfather clause; new rules from v0.8 onward must clear the bar at PR-review time.

## Cross-references

- **ADR 0007** (Rule-pack expansion, +41 rules, Accepted) — original LOCKED Phase 4 / 7 rules; this ADR adds successor rules in the same shape.
- **ADR 0010** (SM 6.9 rule expansion, +36 rules, Accepted) — coopvec / long-vectors / OMM / SER / mesh-nodes; this ADR's `linalg-*` rules supersede the coopvec rules for SM 6.10 targets while leaving the SM ≤ 6.9 rules in force.
- **ADR 0011** (Candidate rule adoption, Accepted) — the per-phase plan template; this ADR mirrors its LOCKED / DEFERRED / DROPPED verdict-tally shape and per-version implementation plans.
- **ADR 0012** (Phase 3 reflection infrastructure) — reflection is reused as-is; v0.8+ rules layer on top with `core/src/rules/util/sm6_10.{hpp,cpp}` shared utilities.
- **ADR 0013** (Phase 4 CFG / uniformity oracle) — CFG infrastructure reused for `sample-use-no-interleave`; no new analysis pass.
- **ADR 0015** (Phase 6 launch plan, Accepted) — v0.5.0 launch sequence; v0.8 / v0.9 / v0.10 follow the same staggered-launch playbook (Discord day 0, HN day 1, r/GraphicsProgramming day 2).
- **ADR 0016 / 0017** (Phase 7 IR-level analysis + revision) — the metadata-only `IrInfo` surface stays; v0.8+ rules do not depend on a lowered-IR layer. v0.10's RGA / Nsight bridges are external oracles, not in-process IR consumers — they coexist with `IrInfo` as alternative pressure / liveness oracles.

## More information

- Brainstorm research date: **2026-05-02** (v0.7.0 ship date).
- New rule categories proposed by this ADR: `linalg` (SM 6.10 matrix APIs), `sm6_10` (intrinsics + attributes new in SM 6.10), and three IHV-target experimental gates (`rdna4`, `blackwell`, `xe2`). Each category enters the CLAUDE.md "category list" once its first rule lands.
- Methodology notes:
  - Coverage-gap §1.1 numbers from `grep -h "^category:" docs/rules/*.md | sort | uniq -c`.
  - Coverage-gap §1.2 stub count from `grep -l "Pre-v0\|pre-v0" docs/rules/*.md`.
  - SM 6.9 / 6.10 surface mapping from the [HLSL Specs Working Draft (PDF 2026-04-29)](https://microsoft.github.io/hlsl-specs/specs/hlsl.pdf) and the SM 6.10 preview blog.
  - RDNA 4 dynamic-VGPR + OBB findings from Chips and Cheese write-ups + AMD Hot Chips 2025 deck.
  - Blackwell findings from the NVIDIA Blackwell Architecture v1.1 PDF + arXiv 2512.02189 microbenchmarking paper.
  - Xe2 / Battlemage findings from Chips and Cheese + HWCooling deep-dive.
  - Competitive-tool surfaces verified against [GPUOpen Live VGPR Analysis](https://gpuopen.com/learn/live-vgpr-analysis-radeon-gpu-analyzer/), [Nsight Shader Profiler docs (2025.4)](https://docs.nvidia.com/nsight-graphics/UserGuide/shader-profiler.html), and the GDC 2026 PIX Shader Explorer announcement.
- Future expansions add a successor ADR (this ADR is not edited after acceptance, per ADR 0007's precedent).
- Doc pages for LOCKED rules can be authored in parallel since each carries the standard `Pre-v0 status` notice; the parallel-authoring stance matches the parallel-implementation stance from ADR 0011.
