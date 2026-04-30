# hlsl-clippy roadmap

A linter for HLSL — performance and correctness rules beyond what `dxc` catches.

**Status as of 2026-04-30:** Phase 0 + Phase 1 complete. The linter ships three rules end-to-end (`pow-const-squared`, `redundant-saturate`, `clamp01-to-saturate`) with machine-applicable `--fix` rewrites, inline `// hlsl-clippy: allow(...)` suppressions, and a `.hlsl-clippy.toml` config (toml++ v3.4.0) supporting per-rule severity, includes/excludes, and per-directory `[[overrides]]`. The Catch2 v3 unit-test suite passes 46/46 under MSVC `/W4 /WX /permissive-` (compiler floor: MSVC 14.44 / VS 17.14 / Build Tools 19.44). Phase 2 implementation plan is queued (ADR 0009: 24 rules across math + saturate-redundancy + misc; parallelizable as a shared-utilities PR + 3 per-category packs). ADR 0010 queues a 36-rule SM 6.7/6.8/6.9 expansion pack (SER, Cooperative Vectors, Long Vectors, OMM, Mesh-in-WG nodes) for Phase 3+.

## What's shipped (Phase 0)

- `pow-const-squared` rule: parses real HLSL, walks the tree-sitter AST, emits a rustc-style diagnostic with source span (diagnostic only; machine-applicable fix lands in Phase 1).
- tree-sitter + tree-sitter-hlsl vendored (`v0.26.8`, grammar at `bab9111`); `tools/treesitter-smoke/` smoke test.
- Slang vendored as git submodule (`v2026.7.1`); `tools/slang-smoke/` smoke test; `cmake/UseSlang.cmake`.
- CI: Windows (MSVC) + Linux (Clang) via `.github/workflows/ci.yml`; lint gate (`.github/workflows/lint.yml`); CodeQL stub (`.github/workflows/codeql.yml`).
- Hardened build: `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (Clang/GCC) scoped to project targets via `hlsl_clippy_warnings` INTERFACE library (`b58fa99`); vendored dependencies compile under their own flags.
- `.clang-tidy` with C++ Core Guidelines check set; `tests/.clang-tidy` scoped separately; CI fails on diagnostics.
- Beachhead corpus: 17 permissively-licensed shaders across vertex/pixel/compute/raytracing/mesh stages under `tests/corpus/`.
- License: Apache-2.0 (switched from MIT per ADR 0006); `NOTICE` and `THIRD_PARTY_LICENSES.md` added.
- 7 architecture decision records (`docs/decisions/0001–0007`) covering parser choice, Slang integration, module layout, devops, licensing, and naming.
- Phase 1 and Phase 2 implementation plans committed as ADR 0008 and ADR 0009.
- 21 rule documentation pages under `docs/rules/` (pow-const-squared + 12 math-category + 5 saturate-redundancy + 3 misc); catalog pre-populated for Phase 2.
- Blog stub live under `docs/blog/` (VitePress); first post on `pow-const-squared` (~1500 words at `docs/blog/pow-const-squared.md`).
- Governance files: `CONTRIBUTING.md` (DCO sign-off, conventional commits), `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`.
- `.github/PULL_REQUEST_TEMPLATE.md` and `ISSUE_TEMPLATE/{bug_report,rule_proposal}.yml`.
- `cli/` and `core/` directory layout (renamed from `crates/`).
- MSVC compiler floor pinned to MSVC 14.44 / Build Tools 19.44 / VS 17.14.

## What's shipped (Phase 1)

- Inline suppression parser — `// hlsl-clippy: allow(rule-id)` (line, block, file scope)
- Declarative TSQuery wrapper for AST-pattern rules
- Quick-fix `Rewriter` framework + `--fix` CLI flag (idempotent application)
- Two new rules with machine-applicable fixes: `redundant-saturate`, `clamp01-to-saturate`
- `.hlsl-clippy.toml` config (toml++ v3.4.0 single-include via FetchContent) — `[rules]`, `[includes]`, `[excludes]`, `[[overrides]]`; walk-up resolver bounded by `.git/`; `--config <path>` CLI flag
- Catch2 v3.5.4 test suite — 46/46 passing
- 22 hand-written fixture files across `tests/fixtures/{phase2,phase3,phase4,phase7}/` covering 76 unique rules with 116 `// HIT(...)` and 68 `// SHOULD-NOT-HIT(...)` annotations
- Corpus expanded to 27 public-licensed shaders (`tests/corpus/SOURCES.md` registry per ADR 0006)

## North star

`hlsl-clippy lint shaders/` produces actionable warnings on patterns that hurt GPU performance or hide correctness bugs. Output is human-readable for IDE use, JSON for CI gates, and quick-fixable wherever the fix is type-safe.

The companion goal: **one short blog post per rule** explaining the GPU reason it matters. The tool is the artifact; the posts are the reputation engine.

## Architecture

- **AST**: [tree-sitter-hlsl](https://github.com/tree-sitter-grammars/tree-sitter-hlsl), patched as needed. Drives syntactic rules and is the substrate for our CFG / data flow.
- **Compile + reflection + IR**: [Slang](https://github.com/shader-slang/slang). Validates the shader, supplies type info / resource bindings / `[numthreads]` / cbuffer layouts via reflection, and emits DXIL / SPIR-V for IR-level rules later.
- **Diagnostic + fix engine**: in-tree, rustc/clippy-style spans with optional machine-applicable fixes.
- **Frontend**: CLI for v0.x, LSP server for v0.5+, VS Code extension thin-wrapping the LSP.

Why this split: Slang's public API exposes compilation + reflection but not AST nodes, so we need a separate parser anyway. Marrying tree-sitter spans with Slang's type / layout reflection gives us syntactic *and* semantic context without linking compiler internals. Slang is actively stewarded, cross-platform from day one, and emits DXIL + SPIR-V + Metal — future-proofing us against single-target lock-in.

## Code standards

- **C++23 baseline** (planned bump from C++20; CMakeLists.txt edit is a separate task). Compiler floors: MSVC 19.40+ (VS 17.10), Clang 18+ with libc++ 17+ or libstdc++ 13+, GCC 14+. Per-target `target_compile_features(... PRIVATE cxx_std_23)` so vendored Slang/tree-sitter keep their own standard.
- **C++23 wins to lean into**: `std::expected<T, Diagnostic>` as the canonical return type across rule and parser stages; `std::print` / `std::println` for diagnostic rendering; deducing `this` for AST visitor bases (no CRTP); `if consteval` for span/range utilities; `[[assume]]` narrowly applied on hot loops; `std::flat_map` / `std::flat_set` for small rule registries and per-file suppression sets.
- **Selective C++26 adoption** — adopt now, gated by feature-test macros: `std::inplace_vector` (`__cpp_lib_inplace_vector`), pack indexing (P2662), `=delete("reason")` (P2573). Defer: static reflection, contracts, `std::execution`.
- Build under MSVC `/W4 /WX /permissive-` and Clang/GCC `-Wall -Wextra -Wpedantic -Werror`. CI fails on any warning.
- **C++ Core Guidelines** enforced via `clang-tidy` (pin tidy 19+) with the `cppcoreguidelines-*`, `bugprone-*`, `modernize-*`, `performance-*`, `readability-*` check sets. `.clang-tidy` is committed; CI runs it. Re-enable `cppcoreguidelines-pro-bounds-array-to-pointer-decay` and `-pro-bounds-constant-array-index` (currently disabled); keep `-pro-bounds-pointer-arithmetic` off only inside `core/parser/` via scoped `NOLINT`.
- **Microsoft GSL** (`gsl::span`, `gsl::not_null`, `gsl::narrow`, `Expects`/`Ensures`) used where the guidelines call for it. Don't use `gsl::owner` (we have `unique_ptr`) or `gsl::string_span` (we have `std::string_view`).
- No raw `new`/`delete` outside of explicit ownership boundaries. RAII everywhere. `std::unique_ptr` / `std::shared_ptr` as the default.
- `clang-format` + `pre-commit` hook. Single style; no bikeshedding.
- **Ban list** (enforced by review + tidy where checkable): no exceptions across the `core` API boundary (use `std::expected`); no `std::endl` in hot paths (use `'\n'` + explicit flush); no `using namespace` at file scope; no C-style casts; no raw owning pointers; no implicit narrowing; no `goto`.

## Licensing

- **Code**: Apache-2.0. Matches Slang upstream (Apache-2.0 + LLVM exception); patent grant + retaliation clause matters for tools in GPU-compilation territory; friction-free for AAA studio / IHV adoption. The `LICENSE` file currently still shows MIT — replacement is a separate task.
- **Documentation, blog posts, rule-catalog pages**: CC-BY-4.0 (footer "© 2026 NelCit, CC-BY-4.0"). Code snippets inside docs stay under project Apache-2.0.
- **`tests/fixtures/`** (hand-written): project Apache-2.0.
- **`tests/corpus/`** (third-party shaders): each file retains its upstream license. `tests/corpus/SOURCES.md` tracks provenance + license per file. Apache/MIT/CC0 only; CC-BY allowed but never baked into the released binary.
- **Contributions**: DCO (Signed-off-by) — not a CLA. Enforced via the DCO GitHub App or a small workflow.
- **Required files**: `LICENSE` (verbatim Apache-2.0), `NOTICE` (short attribution paragraph + per-vendored-dep one-liners), `THIRD_PARTY_LICENSES.md` (full text of each vendored dep's license; ships inside binary releases).
- **Naming**: keep `hlsl-clippy`. Rust-clippy precedent (2014); HLSL is descriptive; tooling like `dxc`, `glslang`, `naga` already coexist. No trademark filing pre-v0.

## Phases

### Phase 0 — First real diagnostic (≈2 weeks) — COMPLETE

Goal: lint a real shader file end-to-end with one rule. No infrastructure-only milestones.

- [x] CMake project, C++20, CLI binary stub
- [x] CMake hardened: `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (Clang/GCC); scoped to first-party targets via `hlsl_clippy_warnings` INTERFACE library (`b58fa99`)
- [x] `.clang-tidy` committed with Core Guidelines check set; CI fails on tidy diagnostics
- [x] Slang vendored as a git submodule + linked as library; smoke test compiles an HLSL string and surfaces Slang's diagnostics (`cmake/UseSlang.cmake`, `v2026.7.1`)
- [x] tree-sitter + tree-sitter-hlsl integrated; smoke test parses an HLSL file and walks the tree (`cmake/UseTreeSitter.cmake`, `v0.26.8`, grammar at `bab9111`)
- [x] First rule: `pow-const-squared` (`pow(x, 2.0)` → `x*x`) producing a diagnostic with span on a real shader, with a machine-applicable fix (diagnostic only in Phase 0; machine-applicable fix lands in Phase 1 alongside the Rewriter framework)
- [x] Beachhead corpus picked and committed under `tests/corpus/` (Apache/MIT/CC0 sources only — Sponza/Bistro PBR shaders, sokol-shdc tests, Filament HLSL exports, public Unity HDRP samples; 17 permissively-licensed shaders across vertex/pixel/compute/raytracing/mesh stages)
- [x] CI on Windows (MSVC) + Linux (Clang). macOS once DXC builds cleanly there. (`.github/workflows/ci.yml`, `.github/workflows/lint.yml`, `.github/workflows/codeql.yml` stub)
- [x] Blog stub live, first post drafted alongside `pow-const-squared` (VitePress under `docs/blog/`; `docs/blog/pow-const-squared.md` ~1500 words)
- [x] Rename `crates/cli` and `crates/core` → `cli/` and `core/` (it's C++, not Rust)

Additional Phase 0 work landed:

- [x] C++ Core Guidelines enforcement via `clang-tidy`: curated check set (`cppcoreguidelines-*`, `bugprone-*`, `modernize-*`, `performance-*`, `readability-*`) with per-target enforcement (ADR 0004); `tests/.clang-tidy` scoped separately
- [x] License switched MIT → Apache-2.0; `NOTICE` and `THIRD_PARTY_LICENSES.md` added (ADR 0006); MSVC compiler floor pinned to MSVC 14.44 / Build Tools 19.44 / VS 17.14
- [x] Architecture decision records: `docs/decisions/0001–0007` (parser, Slang integration, module layout, devops, licensing, naming) plus Phase 1 plan (ADR 0008) and Phase 2 plan (ADR 0009)
- [x] 21 rule documentation pages under `docs/rules/` (pow-const-squared + 12 math-category + 5 saturate-redundancy + 3 misc); catalog pre-populated for Phase 2 implementation
- [x] `docs/` site tree: `_template.md`, `index.md`, `getting-started.md`, `configuration.md`, `architecture.md`, `lsp.md`, `ci.md`, `contributing.md`
- [x] Governance files: `CONTRIBUTING.md` (DCO sign-off, conventional commits), `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1), `SECURITY.md`, `CHANGELOG.md` (Keep a Changelog 1.1.0)
- [x] `.github/PULL_REQUEST_TEMPLATE.md` and `ISSUE_TEMPLATE/{bug_report,rule_proposal}.yml`

### Phase 1 — Rule engine + quick-fix infrastructure (2-3 weeks) — COMPLETE

- [x] `Rule` interface + tree-sitter visitor harness; declarative s-expression query helper for the common syntactic-match case
- [x] Diagnostic format: file, span, code, severity, message, optional fix (rustc-style)
- [x] **Quick-fix framework lands here, not Phase 5.** Machine-applicable suggestions are the clippy comparison; range-based source rewriter built on tree-sitter spans.
- [x] Inline suppression: `// hlsl-clippy: allow(rule-name)` (line-scoped) and block-scoped variants
- [x] Config file: `.hlsl-clippy.toml` for rule severity, includes/excludes, per-directory overrides
- [x] Three rules total, each with quick-fix where safe and a blog post: `pow-const-squared`, `redundant-saturate`, `clamp01-to-saturate`

### Phase 2 — AST-only rule pack (3-4 weeks)

Rules expressible as clang AST patterns — no flow analysis. Group by category so every batch is also a thematic blog series.

**Math simplification:**
- [ ] `pow-to-mul`: `pow(x, 2.0)` → `x*x`, also 3.0/4.0
- [ ] `pow-base-two-to-exp2`: `pow(2.0, x)` → `exp2(x)`
- [ ] `pow-integer-decomposition`: `pow(x, 5.0)` → `x*x*x*x*x` (or pow-by-squaring for ≥4)
- [ ] `inv-sqrt-to-rsqrt`: `1.0 / sqrt(x)` → `rsqrt(x)`
- [ ] `lerp-extremes`: `lerp(a,b,0)` → `a`, `lerp(a,b,1)` → `b`
- [ ] `mul-identity`: `x*1`, `x+0`, `x*0`
- [ ] `sin-cos-pair`: separate `sin(x)`/`cos(x)` on the same `x` → `sincos`
- [ ] `manual-reflect` / `manual-refract`: hand-rolled formula → built-in
- [ ] `manual-distance`: `length(a-b)` → `distance(a,b)`
- [ ] `manual-step`: `x > a ? 1 : 0` → `step(a, x)`
- [ ] `manual-smoothstep`: hand-rolled cubic Hermite → `smoothstep`
- [ ] `length-comparison`: `length(v) < r` → `dot(v,v) < r*r`
- [ ] `manual-mad-decomposition`: `(a*b)+c` split across statements losing FMA fold
- [ ] `dot-on-axis-aligned-vector`: `dot(v, float3(1,0,0))` → `v.x`
- [ ] `length-then-divide`: `v / length(v)` → `normalize(v)` (rsqrt+mul vs sqrt+div)
- [ ] `cross-with-up-vector`: `cross(v, float3(0,1,0))` → negations + moves
- [ ] `countbits-vs-manual-popcount`: hand-rolled popcount → `countbits()`
- [ ] `firstbit-vs-log2-trick`: `log2((float)x)` MSB lookup → `firstbithigh`

**Saturate / clamp / redundancy:**
- [ ] `redundant-saturate`: `saturate(saturate(x))`
- [ ] `clamp01-to-saturate`: `clamp(x, 0, 1)` → `saturate(x)`
- [ ] `redundant-normalize`: `normalize(normalize(x))`
- [ ] `redundant-transpose`: `transpose(transpose(M))`
- [ ] `redundant-abs`: `abs(x*x)`, `abs(saturate(x))`, `abs(dot(v,v))`

**Misc:**
- [ ] `comparison-with-nan-literal`
- [ ] `compare-equal-float`: `==`/`!=` on `float`/`half` (correctness, NaN risk)
- [ ] `redundant-precision-cast`: `(float)((int)x)` round-trips

### Phase 3 — Type / reflection-aware rules (3-4 weeks)

Rules needing Slang's reflection API for binding / layout / type data, married to tree-sitter spans for diagnostics.

**Resource bindings:**
- [ ] `non-uniform-resource-index`: dynamic resource index missing the marker (uses Slang reflection to identify `Texture2D[]`-class params)
- [ ] `cbuffer-padding-hole`: alignment gaps in `cbuffer` layouts
- [ ] `bool-straddles-16b`: `bool` packed across 16-byte boundary
- [ ] `oversized-cbuffer`: cbuffer > N bytes (configurable threshold, default 4 KB)
- [ ] `cbuffer-fits-rootconstants`: small cbuffer (≤8 DWORDs) is root-constant material on D3D12
- [ ] `structured-buffer-stride-mismatch`: element stride not 16-aligned where it should be
- [ ] `unused-cbuffer-field`: declared but never read (needs reachability — moved here from old Phase 2)
- [ ] `dead-store-sv-target`: `SV_Target` written but always overwritten (also moved here)
- [ ] `rwresource-read-only-usage`: `RWBuffer` / `RWTexture` only ever read → demote to SRV
- [ ] `descriptor-heap-no-non-uniform-marker`: `ResourceDescriptorHeap[i]` / `SamplerDescriptorHeap[i]` without `NonUniformResourceIndex` when divergent (SM 6.6+)
- [ ] `descriptor-heap-type-confusion`: sampler assigned to CBV/SRV/UAV slot via wrong heap (SM 6.6+)
- [ ] `all-resources-bound-not-set` (project-level): compiles without `-all-resources-bound` while declaring fully-populated root signatures (driver opts unlocked by the flag)
- [ ] `rov-without-earlydepthstencil`: `RasterizerOrdered*` in PS without `[earlydepthstencil]` and without depth/discard hazards

**Texture / sampling (type-aware):**
- [ ] `samplelevel-with-zero-on-mipped-tex`: explicit `SampleLevel(s, uv, 0)` on a mipped resource (probably wrong)
- [ ] `texture-as-buffer`: `Texture2D` accessed only as 1D linear → suggest `Buffer<>` / `StructuredBuffer<>`
- [ ] `samplegrad-with-constant-grads`: zero gradients → `SampleLevel(0)`
- [ ] `gather-channel-narrowing`: `Gather().r` → `GatherRed` (saves bandwidth on some HW)
- [ ] `samplecmp-vs-manual-compare`: hand-rolled depth compare → `SampleCmp` with comparison sampler
- [ ] `texture-array-known-slice-uniform`: `Texture2DArray.Sample(s, float3(uv, K))` where K is uniform — possibly demote to `Texture2D`
- [ ] `gather-cmp-vs-manual-pcf`: 2x2 unrolled `SampleCmp` for PCF → `GatherCmp` + manual filter weights
- [ ] `texture-lod-bias-without-grad`: `SampleBias` in compute or non-quad-uniform contexts (implicit-derivatives UB)

**Workgroup / threadgroup:**
- [ ] `numthreads-not-wave-aligned`: `[numthreads]` total not a multiple of 32 / 64 (configurable target wave size)
- [ ] `numthreads-too-small`: total < wave size (huge occupancy hit)
- [ ] `groupshared-too-large`: bytes > occupancy threshold

**Interpolators / semantics:**
- [ ] `excess-interpolators`: total `TEXCOORDn` slots exceed hardware budget
- [ ] `nointerpolation-mismatch`: pixel shader treats input as flat but vertex output isn't `nointerpolation`
- [ ] `missing-precise-on-pcf`: depth-compare arithmetic without `precise` qualifier (numerical drift)

**Packed math / fp16 (SM 6.4+):**
- [ ] `pack-clamp-on-prove-bounded`: `pack_clamp_u8` where operand provably in [0,255] → truncating `pack_u8`
- [ ] `min16float-in-cbuffer-roundtrip`: `min16float` param loaded from 32-bit cbuffer field — re-pays 32→16 conversion every read

**Variable rate shading / pixel shader:**
- [ ] `vrs-incompatible-output`: PS writes `SV_Depth` / `SV_StencilRef` / `discard` while pipeline declares per-draw or per-primitive shading rate (silently forces fine-rate shading)
- [ ] `sv-depth-vs-conservative-depth`: PS writes `SV_Depth` where value is monotonically `>=` or `<=` rasterized depth → `SV_DepthGreaterEqual` / `SV_DepthLessEqual` keeps early-Z

**Sampler feedback (SM 6.5+):**
- [ ] `feedback-write-wrong-stage`: `WriteSamplerFeedback*` outside PS (spec-restricted)

**Mesh / amplification (SM 6.5):**
- [ ] `mesh-numthreads-over-128`: `[numthreads]` on mesh/AS entry with X*Y*Z > 128 (PSO creation fails)
- [ ] `mesh-output-decl-exceeds-256`: `out vertices` / `out indices` with N or M > 256 (PSO creation fails)
- [ ] `as-payload-over-16k`: amplification-shader payload struct > 16384 bytes (Slang reflection knows layout)

**Ray tracing (DXR):**
- [ ] `missing-ray-flag-cull-non-opaque`: `TraceRay` against opaque-only geometry without `RAY_FLAG_CULL_NON_OPAQUE` (disables a class of BVH culling)

**Work graphs (SM 6.8):**
- [ ] `nodeid-implicit-mismatch`: `NodeOutput<T>` declarations without explicit `[NodeId(...)]` when struct/downstream node names disagree

### Phase 4 — Control flow + light data flow (4-6 weeks)

Build a CFG over the tree-sitter AST. Add basic uniformity / loop-invariance analysis. Type info from Slang reflection threaded through.

- [ ] `loop-invariant-sample`: texture sample inside loop with loop-invariant UV
- [ ] `cbuffer-load-in-loop`: same cbuffer field reloaded each iteration (CSE hint)
- [ ] `redundant-computation-in-branch`: same expr in both arms of `if/else` → hoist
- [ ] `derivative-in-divergent-cf`: `ddx`/`ddy` / implicit-gradient `Sample` inside non-uniform CF
- [ ] `barrier-in-divergent-cf`: `GroupMemoryBarrier*` inside `if` (UB)
- [ ] `wave-intrinsic-non-uniform`: `WaveActiveSum` etc. in divergent CF
- [ ] `branch-on-uniform-missing-attribute`: dynamically-uniform branch missing `[branch]` hint
- [ ] `small-loop-no-unroll`: constant-bounded loop ≤ N iterations without `[unroll]`
- [ ] `discard-then-work`: significant work after `discard` (helper-lane semantics)
- [ ] `groupshared-uninitialized-read`: read of groupshared cell before any thread writes it
- [ ] `sample-in-loop-implicit-grad`: `Texture.Sample` (implicit derivatives) inside loop / conditional / non-uniform function (cross-lane derivative UB)
- [ ] `early-z-disabled-by-conditional-discard`: `discard` / `clip` reachable from non-uniform CF in PS without `[earlydepthstencil]`
- [ ] `wave-intrinsic-helper-lane-hazard`: wave intrinsics in PS after potential `discard` where helper lanes may participate (distinct from `wave-intrinsic-non-uniform`)
- [ ] `wave-active-all-equal-precheck`: scalarization opportunity for divergent descriptor index where `WaveActiveAllEqual(i)` enables the cheap uniform path
- [ ] `cbuffer-divergent-index`: cbuffer / ICB read with divergent index (serializes on the constant cache)

**Atomics / groupshared:**
- [ ] `interlocked-bin-without-wave-prereduce`: `InterlockedAdd` to small fixed bin set without `WaveActiveSum` / `WavePrefixSum` pre-reduction (32x/64x atomic-traffic drop)
- [ ] `interlocked-float-bit-cast-trick`: hand-rolled `asuint` / sign-flip dance for atomic min/max on floats → SM 6.6 native `InterlockedMin/Max` on float
- [ ] `groupshared-stride-32-bank-conflict`: `groupshared` array indexed `[tid*32+k]` — LDS 32-bank serialization; fix by `+1` padding
- [ ] `groupshared-write-then-no-barrier-read`: thread reads groupshared cell written by another thread without barrier between (UB; distinct from `groupshared-uninitialized-read`)

**Packed math / fp16 (SM 6.4+):**
- [ ] `pack-then-unpack-roundtrip`: `pack_u8(unpack_u8u32(x))`, `f32tof16/f16tof32` round-trips — dead conversion ALU
- [ ] `dot4add-opportunity`: 4-tap int8/uint8 dot via shifts/masks/adds → `dot4add_u8packed` / `dot4add_i8packed` (one DP4a vs 8+ ALU)

**Mesh / amplification (SM 6.5):**
- [ ] `setmeshoutputcounts-in-divergent-cf`: `SetMeshOutputCounts` reachable from non-thread-uniform CF or called more than once (UB)

**Ray tracing (DXR):**
- [ ] `tracerray-conditional`: `TraceRay` / `RayQuery::TraceRayInline` inside `if` whose condition isn't trivially uniform (live-range extension across trace, ray-stack spill)
- [ ] `anyhit-heavy-work`: any-hit shader doing texture sampling beyond alpha-mask, loops, or lighting (move to `closesthit`)
- [ ] `inline-rayquery-when-pipeline-better` / `pipeline-when-inline-better`: wrong-tool selection (20-50% perf delta on shadow/AO passes)

**Sampler feedback (SM 6.5+):**
- [ ] `feedback-every-sample`: `WriteSamplerFeedback*` in hot path with no stochastic gate (spec recommends discarding 99%+ of writes)

**Work graphs (SM 6.8):**
- [ ] `outputcomplete-missing`: `GetGroupNodeOutputRecords` / `GetThreadNodeOutputRecords` not paired with `OutputComplete()` on every CFG path
- [ ] `quad-or-derivative-in-thread-launch-node`: `QuadAny` / `QuadReadAcross*` / `ddx` / `ddy` / implicit-deriv sample inside thread-launch node (no quad structure available)

**Numerical safety:**
- [ ] `acos-without-saturate`: `acos(dot(a,b))` without clamping → NaN risk
- [ ] `div-without-epsilon`: divisor is a length / dot product that can hit zero
- [ ] `sqrt-of-potentially-negative`: signed expression passed to `sqrt`

### Phase 5 — Ergonomics: LSP + IDE (2-3 weeks)

- [ ] LSP server (small JSON-RPC layer; reuse the diagnostic + fix engine)
- [ ] VS Code extension (thin wrapper around LSP)
- [ ] Quick-fix surfaced as VS Code code actions
- [ ] Workspace-aware: respects `.hlsl-clippy.toml`, multi-root projects
- [ ] Slang module / `#include` resolution wired into LSP (cross-file rules need it; Slang handles include paths natively)

### Phase 6 — Launch (v0.5)

- [ ] CI gate mode: exit codes, JSON output, GitHub Actions reporter (annotation format)
- [ ] Documentation site: one page per rule with *why it matters*, before/after, generated DXIL diff where instructive
- [ ] Rule-pack catalog: `math`, `bindings`, `texture`, `workgroup`, `control-flow`, `vrs`, `sampler-feedback`, `mesh`, `dxr`, `work-graphs` togglable in config
- [ ] Launch posts: graphics-programming Discord, r/GraphicsProgramming, Hacker News, Twitter
- [ ] Aggregate the blog posts into a "Why your HLSL is slower than it has to be" series

### Phase 7 — Stretch / research (post-1.0)

IR-level analysis. Slang emits DXIL and SPIR-V; we consume them via existing parsers (DXIL reader, `spirv-tools`, `spirv-cross`) — no need to link Slang internals.

**Memory / register pressure:**
- [ ] `vgpr-pressure-warning`: live-range based estimate; threshold per stage
- [ ] `scratch-from-dynamic-indexing`: dynamic index into local array → register-file fallback
- [ ] `redundant-texture-sample`: identical sample in same basic block (CSE the compiler missed)

**Precision / packing:**
- [ ] `min16float-opportunity`: ALU-bound region using `float` where `min16float` would suffice
- [ ] `unpack-then-repack`: 8888 unpack followed by repack of same lanes
- [ ] `manual-f32tof16`: hand-rolled bit-twiddling → intrinsic

**Ray tracing pack (DXR):**
- [ ] `oversized-ray-payload`
- [ ] `missing-accept-first-hit`: shadow rays without `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`
- [ ] `recursion-depth-not-declared`
- [ ] `live-state-across-traceray`: locals computed before `TraceRay` and read after — they spill to ray stack (IR-level live-range analysis)

**Mesh / amplification shader pack:**
- [ ] `meshlet-vertex-count-bad`
- [ ] `output-count-overrun`

These are research-grade and gated on real adoption. Don't pre-build them.

## Non-goals

- Replacing vendor analyzers (RGA, Intel Shader Analyzer, Nsight). They have ground truth on their own ISA; we surface portable patterns.
- GLSL / WGSL support. Different ecosystems, different rules.
- Auto-fixing every rule. Some fixes (`length` → `dot`) need intent inference; ship as suggestions, not auto-fixes.
- Engine-specific shader rule packs (Unreal USF, Unity HDRP) until core HLSL packs are solid.

## Open questions

- **tree-sitter-hlsl v0.26.8 grammar gap on `cbuffer X : register(b0)`** — confirmed. The published grammar does not parse the explicit register-binding suffix on `cbuffer` declarations and produces an `ERROR` node. Plan: patch upstream as we hit this and other modern-HLSL gaps (templates, work graphs, some SM 6.x). Worst-case fallback: hand-rolled parser for the subset we need. See ADR 0002 for parser-choice rationale.
- **Slang on macOS.** Linux + Windows binaries are stable; macOS builds are improving but Metal-target paths have historically been rocky. Defer macOS CI until Phase 5.
- **DXIL vs SPIR-V for IR rules.** Slang emits both. DXIL is the deployment target for D3D12, but SPIR-V tooling (`spirv-tools`, `spirv-cross`) is more mature for analysis. Prototype IR rules on SPIR-V first.
- **DXC in PATH at runtime.** `slang-smoke` requires DXC at runtime on some paths. Determine whether to bundle DXC in release artifacts or require it on PATH. See ADR 0005.
- **Linux distro floor / libc selection** — Ubuntu 24.04 (glibc 2.39) is the CI baseline; whether to add a `manylinux_2_28` build container before v0.1 is open. libc++ vs libstdc++ on the Clang job TBD. See ADR 0005.
- **Slang version pinning.** Slang's reflection API is more stable than a compiler-internal AST API would be, but still ABI-fluid across releases. Pin a Slang release, bump deliberately, CI catches breakage.
- **Module decomposition (`include/hlslc/` + `libs/{parser,semantic,diag,rules,driver}/` + `apps/{cli,lsp}/`).** Architecture review proposes a finer split than the current `cli/` + `core/` layout. Tracked as ADR 0003 (Proposed) — defer until Phase 1+ when the rule engine lands.
- **Distribution.** Single static binary per OS via GitHub releases. Slang as a static lib if its build allows; otherwise ship the Slang shared library alongside.
