# hlsl-clippy

A linter for HLSL — performance and correctness rules beyond what `dxc` catches.

> Status: pre-v0. Scaffolding only. No working rules yet. See [ROADMAP.md](ROADMAP.md).

## Why

DXC produces compiler errors and language-version warnings. Vendor analyzers (AMD RGA, Intel Shader Analyzer, NVIDIA Nsight) report per-architecture cost. Nothing in between flags portable patterns like `pow(x, 2.0)`, derivatives in divergent control flow, or missing `NonUniformResourceIndex` annotations.

`hlsl-clippy` is that missing layer.

## Build

```
cmake -B build
cmake --build build
./build/hlsl-clippy --help
```

Requires CMake 3.20+ and a C++20 compiler. DXC integration lands in Phase 0 — see [ROADMAP.md](ROADMAP.md).

## Goals

- Catch portable HLSL anti-patterns at edit time and in CI.
- One blog-post-quality explanation per rule.
- Cross-platform CLI + LSP integration.

## Non-goals

- Replacing vendor analyzers.
- GLSL / WGSL (for now).
