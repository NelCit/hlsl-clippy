# hlsl-clippy roadmap

A linter for HLSL — performance and correctness rules beyond what `dxc` catches.

## North star

`hlsl-clippy lint shaders/` produces actionable warnings on patterns that hurt GPU performance or hide correctness bugs (`pow(x, 2.0)`, derivatives in divergent control flow, missing `NonUniformResourceIndex`, etc.).

Output is human-readable for IDE use, JSON for CI gates, and quick-fixable where possible.

## Phases

### Phase 0 — Scaffolding (1-2 weeks)
- [x] CMake project, C++20, CLI binary stub
- [ ] DXC integration: link `dxcompiler` as a library; hello-world that compiles an HLSL string and surfaces its diagnostics
- [ ] CI on Windows (MSVC) + Linux (Clang); macOS later
- [ ] License, README, CONTRIBUTING

### Phase 1 — AST + rule engine (2-3 weeks)
- [ ] Parser decision: DXC's clang-derived AST (link DXC internals) vs tree-sitter-hlsl. Decide by week 3.
- [ ] `Rule` interface + AST visitor
- [ ] Diagnostic format: span, code, message, optional fix (rustc/clang style)
- [ ] Inline suppression: `// hlsl-clippy: allow(rule-name)`
- [ ] Config file: `.hlsl-clippy.toml` for rule severity, includes/excludes
- [ ] First trivial rule: `pow-const-squared`

### Phase 2 — AST-only rule pack (3-4 weeks)
Rules needing only the AST, no data flow:
- [ ] `pow-to-mul`: `pow(x, 2.0)` → `x*x`
- [ ] `length-squared`: `length(v) < r` in a comparison → `dot(v,v) < r*r`
- [ ] `redundant-saturate`: `saturate(saturate(x))`
- [ ] `unused-cbuffer-field`
- [ ] `dead-store-sv-target`

### Phase 3 — Data flow (3-4 weeks)
Rules needing control / data flow:
- [ ] `loop-invariant-sample`: texture sample inside loop with loop-invariant UV
- [ ] `non-uniform-resource-index`: dynamic resource index missing the marker
- [ ] `redundant-computation-in-branch`

### Phase 4 — DXIL-level analysis (4+ weeks)
Hardest rules — operate on the compiled IR via DXC reflection:
- [ ] `derivative-in-divergent-cf`: `ddx`/`ddy`/`Sample` inside non-uniform control flow
- [ ] `vgpr-pressure-warning`: threshold-based register count warning per shader stage
- [ ] `redundant-texture-sample`: duplicate sample of same UV+texture

### Phase 5 — IDE integration (2-3 weeks)
- [ ] LSP server (small JSON-RPC layer in C++ or `cpp-lsp-framework`)
- [ ] VS Code extension
- [ ] Quick-fix suggestions for AST-only rules

### Phase 6 — Launch
- [ ] CI gate mode: exit codes, JSON output, GitHub Actions reporter
- [ ] Documentation site, one page per rule (why it matters, before/after, generated ASM diff)
- [ ] Launch posts: graphics-programming Discord, r/GraphicsProgramming, Hacker News, Twitter
- [ ] Companion blog: one post per rule explaining the GPU reason it matters — this is what builds your reputation

## Non-goals (for now)

- Replacing vendor analyzers (RGA, Intel Shader Analyzer, Nsight). They have ground truth on their own ISA; we surface portable patterns.
- GLSL / WGSL support. Different ecosystems, different rules. Maybe later.
- Auto-fixing every rule. Some fixes (e.g. `length` → `dot`) need type/intent inference; ship as suggestions, not auto-fixes.

## Open questions

- **Parser choice.** DXC's AST is the truth; embedding it means linking DXC internals (heavy but accurate). tree-sitter-hlsl is lighter but its grammar is incomplete and lossy.
- **`#include` resolution.** DXC handles it natively. tree-sitter doesn't. Affects parser choice.
- **Distribution.** Single static binary per OS via GitHub releases. Linux/Windows first; macOS once DXC builds cleanly there.
