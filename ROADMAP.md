# hlsl-clippy roadmap

A linter for HLSL — performance and correctness rules beyond what `dxc` catches.

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

- **C++20**, building under MSVC `/W4 /WX /permissive-` and Clang/GCC `-Wall -Wextra -Wpedantic -Werror`. CI fails on any warning.
- **C++ Core Guidelines** enforced via `clang-tidy` with the `cppcoreguidelines-*`, `bugprone-*`, `modernize-*`, `performance-*`, `readability-*` check sets. `.clang-tidy` is committed; CI runs it.
- **Microsoft GSL** (`gsl::span`, `gsl::not_null`, `gsl::narrow`) used where the guidelines call for it.
- No raw `new`/`delete` outside of explicit ownership boundaries. RAII everywhere. `std::unique_ptr` / `std::shared_ptr` as the default.
- `clang-format` + `pre-commit` hook. Single style; no bikeshedding.

## Phases

### Phase 0 — First real diagnostic (≈2 weeks)

Goal: lint a real shader file end-to-end with one rule. No infrastructure-only milestones.

- [x] CMake project, C++20, CLI binary stub
- [ ] CMake hardened: `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (Clang/GCC)
- [ ] `.clang-tidy` committed with Core Guidelines check set; CI fails on tidy diagnostics
- [ ] Slang vendored as a git submodule + linked as library; smoke test compiles an HLSL string and surfaces Slang's diagnostics
- [ ] tree-sitter + tree-sitter-hlsl integrated; smoke test parses an HLSL file and walks the tree
- [ ] First rule: `pow-const-squared` (`pow(x, 2.0)` → `x*x`) producing a diagnostic with span on a real shader, with a machine-applicable fix
- [ ] Beachhead corpus picked and committed under `tests/corpus/` (Apache/MIT/CC0 sources only — Sponza/Bistro PBR shaders, sokol-shdc tests, Filament HLSL exports, public Unity HDRP samples)
- [ ] CI on Windows (MSVC) + Linux (Clang). macOS once DXC builds cleanly there.
- [ ] Blog stub live, first post drafted alongside `pow-const-squared`
- [ ] Rename `crates/cli` and `crates/core` → `cli/` and `core/` (it's C++, not Rust)

### Phase 1 — Rule engine + quick-fix infrastructure (2-3 weeks)

- [ ] `Rule` interface + tree-sitter visitor harness; declarative s-expression query helper for the common syntactic-match case
- [ ] Diagnostic format: file, span, code, severity, message, optional fix (rustc-style)
- [ ] **Quick-fix framework lands here, not Phase 5.** Machine-applicable suggestions are the clippy comparison; range-based source rewriter built on tree-sitter spans.
- [ ] Inline suppression: `// hlsl-clippy: allow(rule-name)` (line-scoped) and block-scoped variants
- [ ] Config file: `.hlsl-clippy.toml` for rule severity, includes/excludes, per-directory overrides
- [ ] Three rules total, each with quick-fix where safe and a blog post: `pow-const-squared`, `redundant-saturate`, `clamp01-to-saturate`

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

**Texture / sampling (type-aware):**
- [ ] `samplelevel-with-zero-on-mipped-tex`: explicit `SampleLevel(s, uv, 0)` on a mipped resource (probably wrong)
- [ ] `texture-as-buffer`: `Texture2D` accessed only as 1D linear → suggest `Buffer<>` / `StructuredBuffer<>`
- [ ] `samplegrad-with-constant-grads`: zero gradients → `SampleLevel(0)`
- [ ] `gather-channel-narrowing`: `Gather().r` → `GatherRed` (saves bandwidth on some HW)
- [ ] `samplecmp-vs-manual-compare`: hand-rolled depth compare → `SampleCmp` with comparison sampler
- [ ] `texture-array-known-slice-uniform`: `Texture2DArray.Sample(s, float3(uv, K))` where K is uniform — possibly demote to `Texture2D`

**Workgroup / threadgroup:**
- [ ] `numthreads-not-wave-aligned`: `[numthreads]` total not a multiple of 32 / 64 (configurable target wave size)
- [ ] `numthreads-too-small`: total < wave size (huge occupancy hit)
- [ ] `groupshared-too-large`: bytes > occupancy threshold

**Interpolators / semantics:**
- [ ] `excess-interpolators`: total `TEXCOORDn` slots exceed hardware budget
- [ ] `nointerpolation-mismatch`: pixel shader treats input as flat but vertex output isn't `nointerpolation`
- [ ] `missing-precise-on-pcf`: depth-compare arithmetic without `precise` qualifier (numerical drift)

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
- [ ] Rule-pack catalog: `math`, `bindings`, `texture`, `workgroup`, `control-flow` togglable in config
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

- **tree-sitter-hlsl grammar gaps.** It's incomplete around modern HLSL (templates, work graphs, some SM 6.x). Plan: patch upstream as we hit gaps. Worst-case fallback: hand-rolled parser for the subset we need (~2k LOC).
- **Slang on macOS.** Linux + Windows binaries are stable; macOS builds are improving but Metal-target paths have historically been rocky. Defer macOS CI until Phase 5.
- **DXIL vs SPIR-V for IR rules.** Slang emits both. DXIL is the deployment target for D3D12, but SPIR-V tooling (`spirv-tools`, `spirv-cross`) is more mature for analysis. Prototype IR rules on SPIR-V first.
- **Slang version pinning.** Slang's reflection API is more stable than a compiler-internal AST API would be, but still ABI-fluid across releases. Pin a Slang release, bump deliberately, CI catches breakage.
- **Distribution.** Single static binary per OS via GitHub releases. Slang as a static lib if its build allows; otherwise ship the Slang shared library alongside.
- **Beachhead corpus licensing.** Don't commit copyrighted shaders into `tests/corpus/`. Stick to Apache/MIT/CC0 sources or write our own.
