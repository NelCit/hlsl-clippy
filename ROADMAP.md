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
- [x] `pow-to-mul`: `pow(x, 2.0)` → `x*x`, also 3.0/4.0
- [x] `pow-base-two-to-exp2`: `pow(2.0, x)` → `exp2(x)`
- [x] `pow-integer-decomposition`: `pow(x, 5.0)` → `x*x*x*x*x` (or pow-by-squaring for ≥4)
- [x] `inv-sqrt-to-rsqrt`: `1.0 / sqrt(x)` → `rsqrt(x)`
- [x] `lerp-extremes`: `lerp(a,b,0)` → `a`, `lerp(a,b,1)` → `b`
- [x] `mul-identity`: `x*1`, `x+0`, `x*0`
- [x] `sin-cos-pair`: separate `sin(x)`/`cos(x)` on the same `x` → `sincos`
- [x] `manual-reflect`: hand-rolled formula → built-in `reflect()`
- [x] `manual-refract`: hand-rolled formula → built-in `refract()` (sibling of `manual-reflect`; not yet implemented)
- [x] `manual-distance`: `length(a-b)` → `distance(a,b)`
- [x] `manual-step`: `x > a ? 1 : 0` → `step(a, x)`
- [x] `manual-smoothstep`: hand-rolled cubic Hermite → `smoothstep`
- [x] `length-comparison`: `length(v) < r` → `dot(v,v) < r*r`
- [x] `manual-mad-decomposition`: `(a*b)+c` split across statements losing FMA fold
- [x] `dot-on-axis-aligned-vector`: `dot(v, float3(1,0,0))` → `v.x`
- [x] `length-then-divide`: `v / length(v)` → `normalize(v)` (rsqrt+mul vs sqrt+div)
- [x] `cross-with-up-vector`: `cross(v, float3(0,1,0))` → negations + moves
- [x] `countbits-vs-manual-popcount`: hand-rolled popcount → `countbits()`
- [x] `firstbit-vs-log2-trick`: `log2((float)x)` MSB lookup → `firstbithigh`
- [x] `lerp-on-bool-cond`: `lerp(a, b, (float)cond)` where `cond` is bool — portable form is `cond ? b : a` or explicit `select`  *(via ADR 0011)*
- [x] `select-vs-lerp-of-constant`: `lerp(K1, K2, t)` with K1/K2 both constants → explicit `mad(t, K2-K1, K1)`  *(via ADR 0011)*
- [x] `redundant-unorm-snorm-conversion`: explicit `* (1.0/255.0)` after sampling a UNORM texture → drop the dead divide  *(via ADR 0011)*

**Saturate / clamp / redundancy:**
- [x] `redundant-saturate`: `saturate(saturate(x))`
- [x] `clamp01-to-saturate`: `clamp(x, 0, 1)` → `saturate(x)`
- [x] `redundant-normalize`: `normalize(normalize(x))`
- [x] `redundant-transpose`: `transpose(transpose(M))`
- [x] `redundant-abs`: `abs(x*x)`, `abs(saturate(x))`, `abs(dot(v,v))`

**Misc:**
- [x] `comparison-with-nan-literal`
- [x] `compare-equal-float`: `==`/`!=` on `float`/`half` (correctness, NaN risk)
- [x] `redundant-precision-cast`: `(float)((int)x)` round-trips

**ADR 0011 additions:**
- [x] `groupshared-volatile`: `volatile` qualifier on a `groupshared` declaration — meaningless under the HLSL memory model and pessimises LDS scheduling  *(via ADR 0011)*
- [x] `wavereadlaneat-constant-zero-to-readfirst`: `WaveReadLaneAt(x, 0)` → `WaveReadLaneFirst(x)` (skips the lane-index broadcast)  *(via ADR 0011)*
- [x] `loop-attribute-conflict`: both `[unroll]` and `[loop]` on the same loop, or `[unroll(N)]` with N > configurable threshold (default 32)  *(via ADR 0011)*

### Phase 3 — Type / reflection-aware rules (3-4 weeks)

Rules needing Slang's reflection API for binding / layout / type data, married to tree-sitter spans for diagnostics.

**Gating dependency:** [ADR 0012](docs/decisions/0012-phase-3-reflection-infrastructure.md) (Proposed) — the Slang-reflection-into-RuleContext infrastructure must land first. See ADR 0012 §"Implementation sub-phases" for the 3a (infra) → 3b (shared utilities) → 3c (5 parallel rule packs) sequence.

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
- [ ] `byteaddressbuffer-load-misaligned`: `Load2`/`Load3`/`Load4` on `ByteAddressBuffer` at constant offset failing the natural-alignment check (8/12/16)  *(via ADR 0011)*
- [ ] `byteaddressbuffer-narrow-when-typed-fits`: `ByteAddressBuffer.Load4` of a POD that exactly matches a `Buffer<float4>` / `StructuredBuffer<T>` view (cache-path mismatch)  *(via ADR 0011)*
- [ ] `structured-buffer-stride-not-cache-aligned`: stride a multiple of 4 but not of 16 / 32 / 64 (configurable cache-line target)  *(via ADR 0011)*
- [ ] `cbuffer-large-fits-rootcbv-not-table`: cbuffer ≤ 64 KB referenced once per dispatch where a root CBV would dodge the descriptor-table indirection  *(via ADR 0011)*
- [ ] `uav-srv-implicit-transition-assumed`: shader writes UAV `U` then reads SRV `S` where reflection notes `U` and `S` alias (suggestion-grade)  *(via ADR 0011)*

**Texture / sampling (type-aware):**
- [ ] `samplelevel-with-zero-on-mipped-tex`: explicit `SampleLevel(s, uv, 0)` on a mipped resource (probably wrong)
- [ ] `texture-as-buffer`: `Texture2D` accessed only as 1D linear → suggest `Buffer<>` / `StructuredBuffer<>`
- [ ] `samplegrad-with-constant-grads`: zero gradients → `SampleLevel(0)`
- [ ] `gather-channel-narrowing`: `Gather().r` → `GatherRed` (saves bandwidth on some HW)
- [ ] `samplecmp-vs-manual-compare`: hand-rolled depth compare → `SampleCmp` with comparison sampler
- [ ] `texture-array-known-slice-uniform`: `Texture2DArray.Sample(s, float3(uv, K))` where K is uniform — possibly demote to `Texture2D`
- [ ] `gather-cmp-vs-manual-pcf`: 2x2 unrolled `SampleCmp` for PCF → `GatherCmp` + manual filter weights
- [ ] `texture-lod-bias-without-grad`: `SampleBias` in compute or non-quad-uniform contexts (implicit-derivatives UB)
- [ ] `static-sampler-when-dynamic-used`: a sampler whose state never varies across draws → promote to static sampler  *(via ADR 0011)*
- [ ] `mip-clamp-zero-on-mipped-texture`: `MaxLOD = 0` (or `MinMipLevel = 0`) on a sampler bound to a fully-mipped texture (silently disables mip filtering)  *(via ADR 0011)*
- [ ] `comparison-sampler-without-comparison-op`: `SamplerComparisonState` declared but only `Sample`/`SampleLevel` (non-`Cmp` variants) called  *(via ADR 0011)*
- [ ] `anisotropy-without-anisotropic-filter`: `MaxAnisotropy > 1` on a sampler whose `Filter` doesn't request anisotropic filtering (silently ignored)  *(via ADR 0011)*
- [ ] `bgra-rgba-swizzle-mismatch`: shader reads `.rgba` from a `Texture2D<float4>` whose binding maps a `DXGI_FORMAT_B8G8R8A8_UNORM` resource without `.bgra` swizzle  *(via ADR 0011)*
- [ ] `manual-srgb-conversion`: hand-rolled gamma 2.2 / sRGB transfer where the resource format already carries the sRGB conversion (double-applies the curve)  *(via ADR 0011)*

**Workgroup / threadgroup:**
- [ ] `numthreads-not-wave-aligned`: `[numthreads]` total not a multiple of 32 / 64 (configurable target wave size)
- [ ] `numthreads-too-small`: total < wave size (huge occupancy hit)
- [ ] `groupshared-too-large`: bytes > occupancy threshold
- [ ] `compute-dispatch-grid-shape-vs-quad`: `[numthreads(N,1,1)]` chosen for a kernel that reads `ddx`/`ddy` (compute-quad derivatives expect 2x2 quad)  *(via ADR 0011)*
- [ ] `wavesize-attribute-missing`: kernel uses wave intrinsics whose result depends on wave size and lacks `[WaveSize(N)]` / `[WaveSize(min, max)]`  *(via ADR 0011)*

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
- [ ] `sampler-feedback-without-streaming-flag`: `WriteSamplerFeedback*` used without a corresponding tiled-resource binding visible in reflection  *(via ADR 0011)*

**Mesh / amplification (SM 6.5):**
- [ ] `mesh-numthreads-over-128`: `[numthreads]` on mesh/AS entry with X*Y*Z > 128 (PSO creation fails)
- [ ] `mesh-output-decl-exceeds-256`: `out vertices` / `out indices` with N or M > 256 (PSO creation fails)
- [ ] `as-payload-over-16k`: amplification-shader payload struct > 16384 bytes (Slang reflection knows layout)

**Ray tracing (DXR):**
- [ ] `missing-ray-flag-cull-non-opaque`: `TraceRay` against opaque-only geometry without `RAY_FLAG_CULL_NON_OPAQUE` (disables a class of BVH culling)

**Work graphs (SM 6.8):**
- [ ] `nodeid-implicit-mismatch`: `NodeOutput<T>` declarations without explicit `[NodeId(...)]` when struct/downstream node names disagree

**ADR 0011 additions:**
- [ ] `groupshared-union-aliased`: groupshared declaration of two distinct typed views over the same offset (manual `asuint` round-trips or struct hack)  *(via ADR 0011)*
- [ ] `groupshared-16bit-unpacked`: `groupshared min16float` / `groupshared uint16_t` arrays where every access widens to 32 bits before use  *(via ADR 0011)*
- [ ] `wavereadlaneat-constant-non-zero-portability`: `WaveReadLaneAt(x, K)` with constant K when wave size is not pinned via `[WaveSize]`  *(via ADR 0011)*

### Phase 4 — Control flow + light data flow (4-6 weeks)

Build a CFG over the tree-sitter AST. Add basic uniformity / loop-invariance analysis. Type info from Slang reflection threaded through.

**Gating dependency:** [ADR 0013](docs/decisions/0013-phase-4-control-flow-infrastructure.md) (Proposed) — the CFG + uniformity oracle infrastructure (`Stage::ControlFlow` + `Rule::on_cfg` + `CfgEngine` over tree-sitter spans, with bounded inter-procedural inlining and ERROR-node tolerance per ADR 0002) must land first. See ADR 0013 §"Implementation sub-phases" for the 4a (infra) → 4b (shared utilities) → 4c (5 parallel rule packs) sequence. Phase 4 does NOT depend on ADR 0012 / Phase 3 — the CFG works without reflection (with conservative fallbacks where reflection would tighten facts, e.g. helper-lane PS detection).

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
- [ ] `groupshared-stride-non-32-bank-conflict`: groupshared float arrays indexed `[tid*S+k]` for S in {2,4,8,16,64} hitting ≥2-way LDS bank serialization  *(via ADR 0011)*
- [ ] `groupshared-dead-store`: write to a groupshared cell never read on any subsequent path before workgroup exit  *(via ADR 0011)*
- [ ] `groupshared-overwrite-before-barrier`: groupshared cell written, then re-written by the same thread before any `GroupMemoryBarrier*`  *(via ADR 0011)*
- [ ] `groupshared-atomic-replaceable-by-wave`: `InterlockedAdd(gs[0], 1)` / `InterlockedOr(gs[0], mask)` collapsible to `WaveActiveSum` + one representative-lane atomic  *(via ADR 0011)*
- [ ] `groupshared-first-read-without-barrier`: read of `gs[expr]` before the first `GroupMemoryBarrierWithGroupSync` on any path where `expr` may resolve to a cross-thread-written cell  *(via ADR 0011)*

**Packed math / fp16 (SM 6.4+):**
- [ ] `pack-then-unpack-roundtrip`: `pack_u8(unpack_u8u32(x))`, `f32tof16/f16tof32` round-trips — dead conversion ALU
- [ ] `dot4add-opportunity`: 4-tap int8/uint8 dot via shifts/masks/adds → `dot4add_u8packed` / `dot4add_i8packed` (one DP4a vs 8+ ALU)

**Mesh / amplification (SM 6.5):**
- [ ] `setmeshoutputcounts-in-divergent-cf`: `SetMeshOutputCounts` reachable from non-thread-uniform CF or called more than once (UB)
- [ ] `primcount-overrun-in-conditional-cf`: `SetMeshOutputCounts(v, p)` followed by primitive writes guarded by branches whose join produces > p primitives on some path  *(via ADR 0011)*
- [ ] `dispatchmesh-not-called`: amplification entry point with at least one CFG path that does not call `DispatchMesh` (UB)  *(via ADR 0011)*

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
- [ ] `clip-from-non-uniform-cf`: `clip(x)` reachable from non-uniform CF in PS without `[earlydepthstencil]` (sibling rule to `early-z-disabled-by-conditional-discard`)  *(via ADR 0011)*
- [ ] `precise-missing-on-iterative-refine`: a Newton-Raphson / Halley iteration lacking `precise` qualifiers on the residual (fast-math reordering can collapse the iteration)  *(via ADR 0011)*

**ADR 0011 additions:**
- [ ] `divergent-buffer-index-on-uniform-resource`: `buf[i]` with divergent `i` on a buffer whose binding is uniform (Xe-HPG / Ada serialize divergent loads on the K$ / scalar cache)  *(via ADR 0011)*
- [ ] `rwbuffer-store-without-globallycoherent`: writes to a UAV later read on the same dispatch by a different wave without a barrier and without `globallycoherent`  *(via ADR 0011)*
- [ ] `manual-wave-reduction-pattern`: explicit `for` / `InterlockedAdd` / atomics that reproduce a `WaveActiveSum` / `WavePrefixSum`  *(via ADR 0011)*
- [ ] `quadany-quadall-opportunity`: per-lane PS `if (cond)` branch around derivative-bearing ops → wrap in `QuadAny(cond)` to keep helper-lane participation  *(via ADR 0011)*
- [ ] `wave-prefix-sum-vs-scan-with-atomics`: hand-rolled compute-pass scan implemented with groupshared + barriers → `WavePrefixSum` + a single barrier  *(via ADR 0011)*
- [ ] `flatten-on-uniform-branch`: `[flatten]` on an `if` whose condition is dynamically uniform (use `[branch]`)  *(via ADR 0011)*
- [ ] `forcecase-missing-on-ps-switch`: `switch` in PS whose cases each contain texture sampling and that lacks `[forcecase]`  *(via ADR 0011)*

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
- [ ] `groupshared-when-registers-suffice`: groupshared backing for a per-thread temporary array of size ≤ N (configurable, default 8) the compiler can keep in registers (needs IR-level register-pressure estimation; same machinery as `vgpr-pressure-warning`)  *(via ADR 0011)*
- [ ] `buffer-load-width-vs-cache-line`: scalar `Load` per lane that aggregates to a wave's worth of contiguous bytes that would coalesce with `Load4` (needs IR-level per-wave aggregation)  *(via ADR 0011)*

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

## Candidate rule expansion (research, adjudicated by ADR 0011)

The rules listed below are **candidates** sourced from a research pass dated 2026-05-01, surfacing surfaces that the prior locked plan under-covered (groupshared / LDS micro-architecture, ByteAddressBuffer alignment, root-signature ergonomics, mesh / amplification edge cases, helper-lane / quad subtleties, texture-format swizzle traps, and divergence hint mistakes). **All 63 candidates have been adjudicated by [ADR 0011](docs/decisions/0011-candidate-rule-adoption.md)** — 41 LOCKED to specific phases (and inlined into Phases 2 / 3 / 4 / 7 above with a `*(via ADR 0011)*` marker), 20 DEFERRED with one-line reasons, 2 DROPPED as duplicates of already-locked rules. Per-candidate verdicts appear as a suffix on each line below. Future expansions add a successor ADR rather than amending ADR 0011. Surface groupings below (h3) match the rule-pack catalog vocabulary in ADR 0007 / ADR 0010 wherever possible; new categories (`buffer-access`, `root-signature`, `texture-format`, `divergence-hints`, `wave-quad-extras`, `precision`) are proposed alongside the rules that populate them.

### Shared memory / groupshared

- [ ] `groupshared-stride-non-32-bank-conflict`: groupshared float arrays indexed `[tid*S+k]` for S in {2,4,8,16,64} that hit ≥2-way LDS bank serialization on RDNA / NVIDIA 32-bank LDS — *workgroup, target Phase 4* — RDNA 2/3 LDS and Turing/Ada shared memory both have 32 banks of 4 bytes; any stride sharing a non-trivial GCD with 32 still serializes accesses even when the stride isn't exactly 32. *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `groupshared-float64-bank-conflict`: `groupshared double` / `groupshared uint64_t` accesses split across two banks per lane on NVIDIA, doubling LDS issue cost — *workgroup, target Phase 4* — Turing/Ada LDS banks are 32 bits wide; 64-bit accesses always touch two banks per lane and cost 2 issues even when otherwise conflict-free. *(DEFERRED per ADR 0011 — NVIDIA-specific bank width; needs IHV-target gate)*
- [ ] `groupshared-dead-store`: write to groupshared cell that is never read on any subsequent path before workgroup exit — *workgroup, target Phase 4* — wastes LDS bandwidth and pressures occupancy; surfaces refactors where a temporary became a leftover after a code move. *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `groupshared-overwrite-before-barrier`: groupshared cell written, then re-written by the same thread before any `GroupMemoryBarrier*` — *workgroup, target Phase 4* — first write is unobservable to other threads (no barrier separates them), so it is dead and the LDS write is pure waste. *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `groupshared-aos-when-soa-pays`: `groupshared Foo arr[N]` where threads access `arr[tid].field` and `Foo` is wider than one bank — *workgroup, target Phase 4* — AoS in LDS forces tids to stride over the structure size; SoA (`groupshared float field[N]`) keeps lane k mapped to bank k and avoids serialization on RDNA/Ada/Xe-HPG alike. *(DEFERRED per ADR 0011 — heuristic-heavy "wider than one bank"; needs sharper definition + confidence threshold)*
- [ ] `groupshared-atomic-replaceable-by-wave`: `InterlockedAdd(gs[0], 1)` / `InterlockedOr(gs[0], mask)` where the operands are wave-derivable and a `WaveActiveSum` / `WaveActiveBitOr` + a single representative-lane `InterlockedAdd` would replace 32-64 LDS atomics with one — *workgroup, target Phase 4* — distinct from the existing `interlocked-bin-without-wave-prereduce` (small fixed-bin set); this targets single-counter accumulation patterns that drop to one atomic per wave. *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `groupshared-when-registers-suffice`: groupshared backing for a per-thread temporary array of size ≤ N (configurable, default 8) that the compiler can keep in registers — *workgroup, target Phase 4* — every byte spent in LDS comes out of the occupancy budget; on RDNA 32 KB / NVIDIA 100 KB shared per CU/SM the marginal occupancy cliff is steep. *(LOCKED → Phase 7 per ADR 0011 — needs IR-level register-pressure estimation)*
- [ ] `groupshared-non-pow2-size`: groupshared array sized to a non-power-of-two byte count that pushes the workgroup over an occupancy threshold (configurable target architecture: RDNA / NVIDIA / Xe-HPG) — *workgroup, target Phase 4* — RDNA's 32 KB LDS partitions in fixed steps; a 5 KB allocation rounds the same as 8 KB and silently pessimises occupancy. *(DEFERRED per ADR 0011 — needs per-arch occupancy table)*
- [x] `groupshared-volatile`: `volatile` qualifier on a `groupshared` declaration — *workgroup, target Phase 2* — `volatile` on groupshared is meaningless under the HLSL memory model (use `GroupMemoryBarrier*` or `globallycoherent` on UAVs instead) and confuses the optimiser into pessimising LDS scheduling. *(LOCKED → Phase 2 per ADR 0011)*
- [ ] `groupshared-first-read-without-barrier`: read of `gs[expr]` before the first `GroupMemoryBarrierWithGroupSync` on any path where `expr` may resolve to a cell another thread writes — *workgroup, target Phase 4* — distinct from `groupshared-uninitialized-read` (any-thread); this targets the cross-lane race that occurs even when *some* thread has written, just not before the barrier. *(LOCKED → Phase 4 per ADR 0011 — distinct from `groupshared-uninitialized-read`)*
- [ ] `groupshared-union-aliased`: groupshared declaration of two distinct typed views over the same offset (manually packed via `asuint` round-trips or a struct hack) — *workgroup, target Phase 3* — the optimiser cannot reason about aliasing in LDS and falls back to round-tripping every access through memory; surfaces accidental aliasing across reuse phases. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `groupshared-16bit-unpacked`: `groupshared min16float`/`groupshared uint16_t` arrays where every access widens to 32 bits before use — *workgroup, target Phase 3* — RDNA 2/3 packs 16-bit LDS lanes 2-per-bank only when consumed via packed-math intrinsics; widening at the load site collapses the saving. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `groupshared-index-arith-defeats-coalescing`: `gs[base + tid*0 + offset]` / `gs[lane_to_offset(tid)]` patterns that the compiler can't prove monotonic in `tid` — *workgroup, target Phase 4* — RDNA/Ada/Xe-HPG LDS wants lane k → bank k mapping; arithmetic the compiler can't prove monotonic forces a fall-back gather path. *(DEFERRED per ADR 0011 — monotonicity proof is itself a compiler-internal property)*

### Buffer access patterns

- [ ] `byteaddressbuffer-load-misaligned`: `Load2`/`Load3`/`Load4` on `ByteAddressBuffer` at an offset whose constant component fails the natural-alignment check (8/12/16) — *bindings, target Phase 3* — under-aligned widened loads either fault or split into single-DWORD reads on RDNA/Turing/Ada/Xe-HPG; constant-folded offsets make this AST + reflection trivial. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `byteaddressbuffer-narrow-when-typed-fits`: `ByteAddressBuffer.Load4` of POD that exactly matches a `Buffer<float4>` / `StructuredBuffer<T>` view — *bindings, target Phase 3* — typed views go through the texture cache on most IHVs (RDNA TC$, Turing/Ada L1tex), ByteAddressBuffer goes through the K$ on RDNA and L1 on Turing/Ada; one path is usually wrong for the access pattern. *(LOCKED → Phase 3 per ADR 0011 — subsumes `byteaddressbuffer-when-typed-load-suffices`)*
- [ ] `structured-buffer-aos-when-soa-pays`: `StructuredBuffer<Foo>` where every shader reads exactly `field_a` from a wide `Foo` — *bindings, target Phase 4* — the read pulls the entire struct stride into cache for a fraction of the bytes used; SoA via parallel `StructuredBuffer<float>`s typically halves bandwidth on path-tracer hot loops. *(DEFERRED per ADR 0011 — engine-architecture refactor; needs whole-shader read-pattern analysis)*
- [ ] `divergent-buffer-index-on-uniform-resource`: `buf[i]` with divergent `i` on a buffer whose binding is uniform — *bindings, target Phase 4* — Xe-HPG and Ada both serialize divergent loads on the K$ / scalar cache; surfacing the pattern enables the developer to scalarize via `WaveActiveAllEqual` (companion to `wave-active-all-equal-precheck` for descriptor heaps). *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `buffer-load-width-vs-cache-line`: scalar `Load` per lane that aggregates to a wave's worth of contiguous bytes that would coalesce with `Load4` — *bindings, target Phase 4* — RDNA 64-byte cache line / Turing-Ada 128-byte L1 line want one wide transaction per wave; per-lane scalar loads burn extra request slots. *(LOCKED → Phase 7 per ADR 0011 — needs IR-level per-wave aggregation)*
- [ ] `structured-buffer-stride-not-cache-aligned`: stride that is a multiple of 4 but not of 16 / 32 / 64 (configurable to target cache line) — *bindings, target Phase 3* — distinct from the existing `structured-buffer-stride-mismatch` (16-aligned for HLSL packing rules); this targets cache-line wastage where every stride straddles two lines on RDNA/Turing/Ada. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `byteaddressbuffer-when-typed-load-suffices`: ByteAddressBuffer + `asfloat` round-trip that reproduces a `Buffer<float>` load — *bindings, target Phase 3* — the ByteAddressBuffer path bypasses the texture cache on most IHVs; surfaces refactoring opportunities when the resource was originally typed and got "promoted" to ByteAddress without need. *(DROPPED per ADR 0011 — duplicate of `byteaddressbuffer-narrow-when-typed-fits` (same cache-path mechanism, opposite framing))*
- [ ] `rwbuffer-store-without-globallycoherent`: writes to a UAV later read on the same dispatch by a different wave without a barrier and without `globallycoherent` — *bindings, target Phase 4* — without `globallycoherent` the L1 cache on RDNA / L1tex on Turing-Ada caches writes per-CU/SM and reads on a different unit see stale data; companion to the planned barrier rules. *(LOCKED → Phase 4 per ADR 0011)*

### Sampler / static sampler

- [ ] `static-sampler-when-dynamic-used`: shader uses a sampler whose state never varies across draws — *bindings, target Phase 3* — static samplers cost no descriptor slot on D3D12 and are pre-resident on every IHV; promoting saves a descriptor and sometimes a sampler-heap fetch. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `mip-clamp-zero-on-mipped-texture`: `SetSamplerMinLOD`-equivalent of `MaxLOD = 0` (or `MinMipLevel = 0` clamp) on a sampler bound to a fully-mipped texture — *texture, target Phase 3* — silently disables all mip filtering, costs bandwidth (always reads mip 0) and aliasing-quality regressions on terrain / streaming surfaces. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `mip-bias-default-on-streaming`: `MipLODBias = 0` on a sampler used with virtual / tiled / streaming textures where a slight negative bias is the standard recommendation — *texture, target Phase 3* — surfaces a configurable rule (off by default) that streaming-pipeline authors can opt into; avoids the typical "sharp at distance, blurry up close" complaint. *(DEFERRED per ADR 0011 — opt-in / project-policy; awaits opt-in rule configuration surface)*
- [ ] `comparison-sampler-without-comparison-op`: `SamplerComparisonState` declared but only `Sample`/`SampleLevel` (non-`Cmp` variants) called — *texture, target Phase 3* — wastes a sampler descriptor slot and trains readers to expect PCF where there is none; almost always a refactor leftover. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `anisotropy-without-anisotropic-filter`: `MaxAnisotropy > 1` on a sampler whose `Filter` field doesn't request anisotropic filtering — *texture, target Phase 3* — silently ignored on every IHV; surfaces author intent / driver-specific tuning regressions. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `sampler-feedback-without-streaming-flag`: `WriteSamplerFeedback*` used without a corresponding tiled-resource binding visible in reflection — *sampler-feedback, target Phase 3* — sampler feedback that doesn't feed a streaming system is dead bandwidth on IHVs that materialise the feedback texture; companion rule to the existing `feedback-write-wrong-stage` and `feedback-every-sample`. *(LOCKED → Phase 3 per ADR 0011)*

### Push / root constants (D3D12)

- [ ] `root-32bit-constant-pack-mismatch`: cbuffer field declared at the root signature's 32-bit-constants slot whose HLSL type's natural alignment doesn't match the root constant's slot index — *bindings (proposed category: `root-signature`), target Phase 3* — silent miscompare on D3D12; root constants have no per-field alignment, so a `float3` followed by a `float` packs differently than a CBV would. *(DEFERRED per ADR 0011 — needs application-side root signature shape; awaits project-level root-signature input)*
- [ ] `cbuffer-large-fits-rootcbv-not-table`: cbuffer ≤ 64 KB referenced once per dispatch where a root CBV would dodge the descriptor-table indirection — *bindings (`root-signature`), target Phase 3* — companion to `cbuffer-fits-rootconstants`; root CBVs save a descriptor heap dereference on every IHV. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `root-signature-shape-overflows-fast-path`: root signature exceeding 64 DWORDs total — *bindings (`root-signature`), target Phase 3* — D3D12 spec limit; some IHVs spill to memory beyond a smaller threshold (configurable: default 16 DWORDs to match driver fast-paths on RDNA / Ada). *(DEFERRED per ADR 0011 — depends on IHV fast-path numeric thresholds the linter does not yet vendor)*
- [ ] `root-constant-rebind-per-draw`: small per-draw cbuffer field that varies every draw call sitting in a CBV table when root constants would avoid the rebind — *bindings (`root-signature`), target Phase 3* — surfaces a project-level pattern that the developer can validate against their root signature. *(DEFERRED per ADR 0011 — application-level per-draw variability invisible to shader)*

### Mesh / amplification (beyond the locked set)

- [ ] `payload-output-mismatch`: AS payload struct declared with field count / types differing from the MS `in payload` declaration — *mesh, target Phase 3* — produces a hard PSO link error on D3D12; surfacing it at lint time is friendlier than the runtime diagnostic. *(DEFERRED per ADR 0011 — Slang/DXC already emit hard PSO link error; revisit with "explain compiler errors better" surface)*
- [ ] `primcount-overrun-in-conditional-cf`: `SetMeshOutputCounts(v, p)` followed by primitive writes guarded by branches whose join produces > p primitives on some path — *mesh, target Phase 4* — UB on RDNA/Ada/Xe-HPG; companion to the locked `setmeshoutputcounts-in-divergent-cf` which targets the call site, not the writer side. *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `as-launch-grid-too-small`: `DispatchMesh(x,y,z)` with `x*y*z` smaller than one wave on the target architecture — *mesh, target Phase 4* — wastes the AS dispatch entirely; on RDNA the wave is 32/64, on Ada it is 32, on Xe-HPG it is 16/32 — configurable. *(DEFERRED per ADR 0011 — `DispatchMesh` args often runtime; AST-only detection too noisy)*
- [ ] `mesh-vertex-output-soa-mismatch`: per-vertex output struct interleaves position / attribute fields in a way that misses the implicit SoA expansion the driver expects — *mesh, target Phase 3* — Ada / RDNA 3 mesh-shader hardware lays out vertex outputs SoA; AoS authoring forces a re-pack on emit. *(DEFERRED per ADR 0011 — vendor-specific layout; needs IHV-target gate)*
- [ ] `as-payload-write-in-divergent-cf`: writes to the AS payload struct under non-uniform CF — *mesh, target Phase 4* — distinct from the locked `setmeshoutputcounts-in-divergent-cf`; AS payload writes under divergent CF latch the last-writer-wins lane on D3D12, which is rarely what the author wanted. *(DEFERRED per ADR 0011 — close enough to `setmeshoutputcounts-in-divergent-cf` that suppression semantics need explicit design)*
- [ ] `dispatchmesh-not-called`: amplification entry point with at least one CFG path that does not call `DispatchMesh` — *mesh, target Phase 4* — UB; trivially observable from the CFG. *(LOCKED → Phase 4 per ADR 0011)*

### Compute pipeline

- [ ] `compute-dispatch-grid-shape-vs-quad`: `[numthreads(N,1,1)]` chosen for a kernel that reads `ddx`/`ddy` (compute-quad derivatives) — *workgroup, target Phase 3* — SM 6.6 compute-quad derivatives expect a 2x2 quad in the X/Y plane; 1D dispatch packs four threads in X and produces nonsense derivatives. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `wavesize-attribute-missing`: kernel uses wave intrinsics in a way whose result depends on wave size and lacks `[WaveSize(N)]` / `[WaveSize(min, max)]` — *workgroup, target Phase 3* — without the attribute, RDNA 1/2/3 may run wave32 or wave64, Turing/Ada always wave32, Xe-HPG wave8/16/32; results that index by lane count silently change. *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `numthreads-bad-for-1d-workload`: `[numthreads(8,8,1)]` on a kernel that reads `dispatchThreadID.x` only and ignores `.y` / `.z` — *workgroup, target Phase 3* — wastes 7/8 of the threadgroup; surfaces a refactor where a 2D kernel was repurposed for a 1D pass. *(DEFERRED per ADR 0011 — heuristic too noisy on legitimate 2D-indexed kernels)*
- [ ] `numthreads-z-greater-than-one-without-3d-tid-use`: `[numthreads(X,Y,Z)]` with `Z > 1` while `dispatchThreadID.z` / `groupThreadID.z` are unused — *workgroup, target Phase 3* — same family as above; the Z dimension is pure overhead. *(DEFERRED per ADR 0011 — same heuristic family as above)*
- [ ] `groupshared-after-early-return`: groupshared use after a non-uniform `return` / `discard` (compute) on some lane — *workgroup, target Phase 4* — barriers downstream may deadlock; modern compute shaders run unified-helper-lane semantics that this rule documents. *(DEFERRED per ADR 0011 — close to locked `barrier-in-divergent-cf`; needs clean separation spec)*

### Numerical / precision

- [ ] `min16float-subnormal-flush-mismatch`: arithmetic on `min16float` whose result enters a subnormal range that the target IHV flushes-to-zero by default — *math (proposed category: `precision`), target Phase 3* — RDNA and Ada flush fp16 subnormals by default; Xe-HPG preserves them — silently changes the result.  *(DEFERRED per ADR 0011 — IHV-default-flush behaviour varies; needs IHV-target gate and flush-mode reflection accessor)*
- [ ] `clip-from-non-uniform-cf`: `clip(x)` reachable from non-uniform CF in PS without `[earlydepthstencil]` — *control-flow, target Phase 4* — close to the locked `early-z-disabled-by-conditional-discard` but `clip()` has its own semantics distinct from `discard`; surfaces explicitly so the suppression scope is independent.  *(LOCKED → Phase 4 per ADR 0011)*
- [x] `lerp-on-bool-cond`: `lerp(a, b, (float)cond)` where `cond` is a bool — *math, target Phase 2* — produces a `select` codegen on most IHVs but a true `lerp` (mul + mad) on others; portable form is `cond ? b : a` or explicit `select`.  *(LOCKED → Phase 2 per ADR 0011)*
- [x] `select-vs-lerp-of-constant`: `lerp(K1, K2, t)` with both K1/K2 constants — *math, target Phase 2* — the compiler may not fold to `K1 + (K2-K1)*t` on every IHV; explicit `mad(t, K2-K1, K1)` makes the intent compile-portable.  *(LOCKED → Phase 2 per ADR 0011)*
- [ ] `saturate-then-multiply-by-one`: `saturate(x) * 1.0` and similar `*1` combinators — *math, target Phase 2* — companion to the locked `mul-identity` rule but specifically targets the `saturate(...) * 1.0` idiom that survives template / macro expansion.  *(DROPPED per ADR 0011 — strict subset of the locked `mul-identity` rule; macro-expansion concern is a refinement of that rule, not a new one)*
- [ ] `precise-missing-on-iterative-refine`: a Newton-Raphson / Halley iteration that lacks `precise` qualifiers on the residual — *math (`precision`), target Phase 4* — fast-math reordering on Ada / RDNA / Xe-HPG can collapse the iteration to a no-op; surfaces a footgun for analytic-derivative SDF / collision code.  *(LOCKED → Phase 4 per ADR 0011)*

### Resource state / barriers (documentation-grade)

- [ ] `uav-srv-implicit-transition-assumed`: shader writes to UAV `U` then reads from SRV `S` where reflection notes `U` and `S` alias — *bindings, target Phase 3* — D3D12 requires an explicit barrier; surfacing the alias from reflection lets the developer audit the application-side barrier.  *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `transient-resource-not-aliased`: dispatch uses two UAVs whose lifetimes (write-then-read) don't overlap and could share memory via a placed-resource alias — *bindings, target Phase 3* — Suggestion-grade; saves VRAM on render-graph-managed engines (Frostbite / Granite-style).  *(DEFERRED per ADR 0011 — render-graph / placed-resource alias surface is application-side; awaits project-level memory graph input)*

### Wave / lane intrinsics (extras)

- [x] `wavereadlaneat-constant-zero-to-readfirst`: `WaveReadLaneAt(x, 0)` with constant zero — *control-flow (proposed category: `wave-quad-extras`), target Phase 2* — `WaveReadLaneFirst(x)` is the idiomatic spelling and lets the compiler skip the lane-index broadcast on RDNA / Ada.  *(LOCKED → Phase 2 per ADR 0011)*
- [ ] `wavereadlaneat-constant-non-zero-portability`: `WaveReadLaneAt(x, K)` with constant K when the wave size is not pinned via `[WaveSize]` — *control-flow (`wave-quad-extras`), target Phase 3* — K may be out of range on wave32 vs wave64; surfaces a portability bug between RDNA wave64 and Ada wave32.  *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `manual-wave-reduction-pattern`: explicit `for`/`InterlockedAdd`/atomics that reproduce a `WaveActiveSum` / `WavePrefixSum` — *control-flow (`wave-quad-extras`), target Phase 4* — saves 32-64 ALU ops + the LDS / atomic round-trip on every modern IHV.  *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `quadany-quadall-opportunity`: `if (cond)` in PS where `cond` is per-lane and the branch body only executes derivative-bearing ops; could become `if (QuadAny(cond))` to keep helper-lane participation — *control-flow (`wave-quad-extras`), target Phase 4* — companion (not duplicate) of the locked `quadany-replaceable-with-derivative-uniform-branch` which detects the *opposite* direction (replace `QuadAny` with derivative-uniform branch).  *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `wave-prefix-sum-vs-scan-with-atomics`: hand-rolled compute-pass scan implemented with groupshared + barriers — *control-flow (`wave-quad-extras`), target Phase 4* — `WavePrefixSum` + a single barrier collapses the multi-step scan on RDNA / Ada / Xe-HPG.  *(LOCKED → Phase 4 per ADR 0011)*

### Texture format

- [ ] `bgra-rgba-swizzle-mismatch`: shader reads `.rgba` from a `Texture2D<float4>` whose binding maps a `DXGI_FORMAT_B8G8R8A8_UNORM` resource without a corresponding `.bgra` swizzle in the sample-site code — *texture (proposed category: `texture-format`), target Phase 3* — silently inverts red and blue channels; surfaces a real bug for IMGUI / UI pipelines that mix swap-chain BGRA with R8G8B8A8 SRGB sampling.  *(LOCKED → Phase 3 per ADR 0011)*
- [x] `redundant-unorm-snorm-conversion`: explicit `* (1.0/255.0)` after sampling a UNORM texture — *math, target Phase 2* — UNORM sampling already returns `[0,1]`; the divide is dead arithmetic on every IHV.  *(LOCKED → Phase 2 per ADR 0011)*
- [ ] `manual-srgb-conversion`: hand-rolled gamma 2.2 / sRGB transfer in shader code where the resource format already carries the sRGB conversion — *texture (`texture-format`), target Phase 3* — double-applies the curve; common when a pipeline migrates from `R8G8B8A8_UNORM` to `R8G8B8A8_UNORM_SRGB`.  *(LOCKED → Phase 3 per ADR 0011)*
- [ ] `format-bit-width-mismatch-on-load`: `Buffer<float4>` viewing a 16-bit-per-channel resource without explicit conversion intent — *texture (`texture-format`), target Phase 3* — driver inserts a per-load convert that masks bandwidth wins from the narrower format.  *(DEFERRED per ADR 0011 — without explicit conversion intent surface, the rule conflicts with valid format-narrowing patterns; awaits intent-annotation surface)*

### Branch / divergence hints

- [ ] `flatten-on-uniform-branch`: `[flatten]` attribute on an `if` whose condition is dynamically uniform — *control-flow (proposed category: `divergence-hints`), target Phase 4* — `[flatten]` forces both arms to execute even on the cheap path; on uniform branches `[branch]` is the right choice (and on RDNA/Ada/Xe-HPG `[branch]` lets the compiler skip the inactive arm entirely).  *(LOCKED → Phase 4 per ADR 0011)*
- [ ] `branch-on-trivially-constant-cond`: `[branch]` on an `if` whose condition is provably constant after Slang reflection of cbuffer specialisation constants — *control-flow (`divergence-hints`), target Phase 4* — the attribute is dead; the compiler removes the branch but the attribute may suppress later optimisation passes.  *(DEFERRED per ADR 0011 — needs cbuffer specialisation-constant analysis; awaits specialisation-aware fold)*
- [ ] `forcecase-missing-on-ps-switch`: `switch` in PS whose cases each contain texture sampling and that lacks `[forcecase]` — *control-flow (`divergence-hints`), target Phase 4* — without `[forcecase]` the compiler may unroll the switch into chained `if`s, breaking quad-uniform sampling on RDNA / Ada.  *(LOCKED → Phase 4 per ADR 0011)*
- [x] `loop-attribute-conflict`: both `[unroll]` and `[loop]` on the same loop, or `[unroll(N)]` with N exceeding a configurable threshold (default 32) — *control-flow (`divergence-hints`), target Phase 2* — silently picks one; explicit conflict is almost always a refactor leftover.  *(LOCKED → Phase 2 per ADR 0011)*

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
