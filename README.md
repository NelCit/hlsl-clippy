# hlsl-clippy

**A static linter for HLSL — performance and correctness rules beyond what `dxc` catches.**

[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/build-placeholder-lightgrey.svg)](#)
[![Latest Release](https://img.shields.io/badge/release-pre--v0-lightgrey.svg)](#)

> **Status:** pre-v0 — scaffold only. No working rules yet. See [ROADMAP.md](ROADMAP.md) for the full plan.

`dxc` reports compiler errors. Vendor analyzers (AMD RGA, Intel Shader Analyzer, NVIDIA Nsight) report per-architecture instruction costs. Neither catches the portable anti-patterns that silently degrade GPU performance across every target: `pow(x, 2.0)` spending multiple clock cycles on a transcendental, derivatives sampled inside divergent control flow, or dynamic resource indices missing `NonUniformResourceIndex`. `hlsl-clippy` is that missing layer.

<!-- TODO: terminal cast -->

---

## Why hlsl-clippy

### Problem 1 — Unnecessary transcendental functions

`dxc` compiles `pow(x, 2.0)` without complaint. On every GPU target, this costs the same as `pow(x, Pi)`: a transcendental. The fix is trivial.

```hlsl
// Before — dxc accepts this silently
float attenuation = pow(distance, 2.0);

// After — same result, far cheaper
float attenuation = distance * distance;
```

### Problem 2 — Derivatives in divergent control flow

`ddx`, `ddy`, and implicit-LOD texture samples require quad-group uniformity. Inside non-uniform `if` branches, results are undefined and differ per driver. `dxc` issues no diagnostic. hlsl-clippy (Phase 4) will flag every call site.

```hlsl
// Before — undefined behavior, silent
if (material_id != 0) {
    color = albedo.Sample(s, uv);  // derivative across a non-uniform branch
}

// After — hoist the sample
float4 sampled = albedo.Sample(s, uv);
if (material_id != 0) {
    color = sampled;
}
```

### Problem 3 — Missing NonUniformResourceIndex

Dynamic descriptor indexing without `NonUniformResourceIndex` is legal HLSL but undefined behavior on D3D12 if the index is not wave-uniform. The validator does not catch it; the bug appears only on some hardware under some drivers.

```hlsl
// Before — UB on non-uniform index
Texture2D textures[64];
float4 sample_tex(uint id, float2 uv, SamplerState s) {
    return textures[id].Sample(s, uv);
}

// After — correct
float4 sample_tex(uint id, float2 uv, SamplerState s) {
    return textures[NonUniformResourceIndex(id)].Sample(s, uv);
}
```

---

## Quick start

> **Note:** packaging is planned for Phase 6. Until then, build from source.

**Prerequisites:** CMake 3.20+, a C++20 compiler (MSVC 2022 or Clang 16+).

```sh
git clone https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy
cmake -B build
cmake --build build
```

**Basic usage** (lint subcommand is a stub until Phase 1):

```sh
./build/hlsl-clippy --help
./build/hlsl-clippy --version
./build/hlsl-clippy lint shader.hlsl   # prints "not yet implemented"
```

**Planned output format** (as of Phase 1):

```
shader.hlsl:14:21: warning[pow-const-squared]: pow(x, 2.0) is equivalent to x*x
  |
14|     float d = pow(dist, 2.0);
  |               ^^^^^^^^^^^^^^
  = help: replace with `dist * dist`
```

---

## Comparison

| | hlsl-clippy | dxc warnings | AMD RGA / Intel SA | NVIDIA Nsight |
|---|---|---|---|---|
| Portable anti-patterns | Planned | No | No | No |
| Correctness (NURI, divergent CF) | Planned | Partial | No | Partial |
| Per-ISA instruction cost | No | No | Yes | Yes |
| Register pressure | Planned (Phase 4) | No | Yes | Yes |
| CI gate / JSON output | Planned (Phase 6) | Flags only | No | No |
| LSP / editor integration | Planned (Phase 5) | Via extension | No | Yes |
| Machine-applicable fixes | Planned | No | No | No |
| Cross-platform binary | Planned | Windows/Linux | Per-vendor | Windows |

hlsl-clippy is not a replacement for vendor analyzers. They have ground truth on their own ISA. hlsl-clippy surfaces portable patterns that affect every target and that no existing tool flags.

---

## Current status

| Phase | Description | State |
|---|---|---|
| 0 | Scaffolding: CMake, CLI stub, CI, governance | In progress |
| 1 | AST + rule engine, first rule (`pow-const-squared`) | Planned |
| 2 | AST-only rule pack (5 rules) | Planned |
| 3 | Data-flow rules (3 rules) | Planned |
| 4 | DXIL-level analysis (3 rules) | Planned |
| 5 | LSP server + VS Code extension | Planned |
| 6 | Launch: CI gate, docs site, rule pages | Planned |

See [ROADMAP.md](ROADMAP.md) for per-phase detail and open questions.

---

## Links

- [ROADMAP.md](ROADMAP.md) — phased development plan
- [docs/](docs/README.md) — documentation site seed (VitePress scaffold)
- [docs/rules/index.md](docs/rules/index.md) — rule catalog
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev setup, rule authoring, PR process
- Blog series: "Why your HLSL is slower than it has to be" — planned for Phase 6

---

## License

Licensed under the [Apache License, Version 2.0](LICENSE).

Copyright 2026 NelCit and contributors.

Vendored dependencies retain their own licenses; see [NOTICE](NOTICE) and
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for details.
