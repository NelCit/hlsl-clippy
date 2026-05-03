---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [compiler, architecture, phase-0]
---

# Compiler choice — Slang

## Context and Problem Statement

`shader-clippy` needs an HLSL compiler/reflection backend to validate input shaders, surface type and binding information for type-aware rules (Phase 3+), and emit DXIL/SPIR-V for IR-level rules (Phase 7). The choice determines the project's cross-platform reach, ABI stability, and how much compiler-internal plumbing we have to maintain.

## Decision Drivers

- **Cross-platform target support.** Linux + Windows are required day one; macOS / Metal is a stretch goal. The compiler must emit DXIL (D3D12) and SPIR-V (Vulkan) at minimum.
- **Active stewardship.** A linter that pins a dead compiler ages out fast. We want a backend with a maintained release cadence and a community that fixes regressions.
- **Public reflection API.** Type information, cbuffer layouts, `[numthreads]`, and resource bindings must be reachable through a stable, public API — not by reaching into private headers.
- **Avoid linking compiler-internal AST.** Going through internal C++ headers (LLVM/Clang style) buys accuracy at the price of every-release ABI churn and a build burden that crushes a small project.
- **License compatibility.** Whatever we link must be permissively licensed (see ADR 0006).

## Considered Options

1. **DXC (DirectX Shader Compiler).** Microsoft's clang-derived HLSL frontend. Public reflection on DXIL output exists; AST is internal-only.
2. **In-tree LLVM-HLSL frontend.** Build/vendor the upstream LLVM HLSL frontend (the eventual successor to DXC). Heavy build, immature reflection.
3. **Slang.** Khronos/NVIDIA-stewarded shader compiler. Public C/C++ API for compilation + reflection; emits DXIL + SPIR-V + Metal + WGSL; cross-platform builds.

## Decision Outcome

Chosen option: **Slang.** Vendored as a git submodule under `third_party/slang/` with a pinned tag (see ADR 0005). Used for compile, reflection, and IR emission. AST handling lives in tree-sitter-hlsl (see ADR 0002) — Slang's AST is internal-only, so we don't even attempt to consume it.

This decision is locked. Subsequent discussions of the "compiler internals AST" path should frame it as a Slang-internal-headers option, not DXC.

### Consequences

Good:

- One backend covers DXIL + SPIR-V + Metal + WGSL — future-proofs us against single-target lock-in.
- Reflection API is documented and public; ABI churn is real but bounded by Slang's release tags.
- License (Apache-2.0 + LLVM exception) matches our project license (see ADR 0006), no compatibility friction.
- Active development means new HLSL features land before we'd have to backport them.

Bad:

- Slang's build is heavy (own submodules, glslang + spirv-tools transitively) — cold CI builds are 20+ minutes; mitigated by submodule install-prefix caching (see ADR 0005).
- Slang is not thread-safe at the `IGlobalSession` level; we maintain one global session and a per-worker `ISession` pool.
- ABI fluidity across releases — `<slang.h>` must never appear in `include/hlslc/`; isolation is enforced by ADR 0003.
- macOS Metal-target paths have historically been rocky; macOS CI is deferred until Phase 5.

### Confirmation

- A Phase 0 smoke test compiles an HLSL string via `slang::IGlobalSession` and surfaces Slang's diagnostics — listed in ROADMAP Phase 0.
- CI lints first-party headers for any `slang.h`-family include leaking into `include/hlslc/`.
- Slang version is pinned in `cmake/SlangVersion.cmake` (`SLANG_REVISION` variable). Bumps are deliberate, with the matrix able to override to test next-rev.

## Pros and Cons of the Options

### DXC

- Good: ground-truth HLSL frontend; what every D3D12 game ships against.
- Good: best HLSL-feature parity (DXC is the spec-defining implementation).
- Bad: public API is mostly the compile interface; reflection is via DXIL post-hoc, not full type info.
- Bad: AST is internal-only — same problem as Slang, with worse cross-platform support (Linux build is real, macOS is rough).
- Bad: only emits DXIL natively. SPIR-V via Vulkan-Memory-Allocator's `dxc -spirv` extensions, not a primary target.
- Bad: explicitly **not chosen** (see project memory `feedback_compiler_choice.md`).

### In-tree LLVM-HLSL frontend

- Good: aligns with where the long-term Microsoft direction is going.
- Bad: pre-1.0; reflection API not yet shaped; no shipping consumers to validate against.
- Bad: vendoring LLVM into our build tree is a six-month side quest by itself.
- Bad: massive compile-time burden on contributors and CI.

### Slang (chosen)

- Good: cross-platform; Linux + Windows + macOS builds shipped by upstream.
- Good: emits DXIL + SPIR-V + Metal + WGSL — the linter's portable-pattern story is honest.
- Good: public reflection API documented + maintained.
- Good: Apache-2.0 + LLVM exception aligns with our license (ADR 0006); patent grant matters in GPU-compilation territory.
- Good: strong stewardship (Khronos governance + NVIDIA contribution).
- Bad: heavy build; ABI not fully frozen across releases; thread-safety constrained at the global-session level. All mitigatable.

## Links

- Project memory: `feedback_compiler_choice.md` (locked decision).
- Verbatim research: `_research/architecture-review.md` §2 (external dependencies).
- Related: ADR 0002 (parser), ADR 0003 (module decomposition isolating Slang headers), ADR 0005 (CI caching for Slang), ADR 0006 (license alignment).
