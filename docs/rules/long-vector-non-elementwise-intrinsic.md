---
id: long-vector-non-elementwise-intrinsic
category: long-vectors
severity: error
applicability: none
since-version: v0.2.0
phase: 2
---

# long-vector-non-elementwise-intrinsic

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A call to a non-elementwise HLSL intrinsic — `cross`, `length`, `normalize`, `dot` (under specific arity rules), `transpose`, `mul` against a matrix, `determinant` — applied to a `vector<T, N>` with `N >= 5`. The SM 6.9 long-vector specification (DXIL vectors, proposal 0030) extends `vector<T, N>` to support `5 <= N <= 1024`, but only for *elementwise* intrinsics: arithmetic operators, `abs`, `sqrt`, `exp`, `log`, `min`, `max`, `mad`, `lerp`, `step`, `saturate`, comparison operators, and the like. Geometric, structural, and matrix-shape intrinsics are explicitly out of scope and produce a hard DXC validation error. The rule is pure AST: intrinsic name plus the literal vector type on the argument.

## Why it matters on a GPU

The SM 6.9 long-vector feature is a codegen change: DXIL gains first-class `vector<T, N>` types so the compiler can emit one DXIL operation that the IHV scalarizer expands into the natural wave shape of the target GPU. NVIDIA Ada Lovelace's compiler emits packed-math FP16 sequences for `vector<float16_t, 16>`; AMD RDNA 3's compiler maps `vector<float, 8>` onto the wave-wide register file with one VALU lane per element pair; Intel Xe-HPG's compiler uses the SIMD16/SIMD32 hardware width. The point of the feature is to skip the manual array-of-vec4 unrolling that pre-SM-6.9 code wrote.

Crucially, the elementwise property is what makes the codegen tractable. `cross(a, b)` is geometrically defined only for 3-vectors; `length(v)` is `sqrt(dot(v, v))` and the `dot` reduction tree is a structural operation that the long-vector lowering does not implement; `transpose(M)` reshapes data, not elements; `mul(M, v)` is matrix-vector multiply with a fundamentally different cost model (cooperative vectors are the right surface for that). The DXIL spec permits each intrinsic on the legacy 2/3/4-wide vector types and forbids extending them — the compiler emits a precise validation error.

Catching this at lint time replaces a DXC error message with a source-located diagnostic, and points the author at the right alternative: split the long vector into chunks of 3 for `cross`, use `sqrt(dot(v, v))` written explicitly with elementwise operations and a manual reduction, or move to cooperative vectors for matrix-vector multiply.

## Examples

### Bad

```hlsl
// length() is not defined for long vectors — DXC validation error.
float8 v = LoadLongVec(); // vector<float, 8>
float  L = length(v);     // ERROR: non-elementwise intrinsic on long vector
```

### Good

```hlsl
// Spell out length using elementwise ops + a manual reduction.
float8 v = LoadLongVec();
float8 sq = v * v;
float  L  = sqrt(sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5] + sq[6] + sq[7]);
```

## Options

none

## Fix availability

**none** — Replacement depends on what the author wanted (Euclidean length? per-channel norm? something else); the rule names the intrinsic and points at the spec.

## See also

- Related rule: [long-vector-in-cbuffer-or-signature](long-vector-in-cbuffer-or-signature.md) — long vectors not allowed at IO boundaries
- Related rule: [long-vector-typed-buffer-load](long-vector-typed-buffer-load.md) — long vectors not allowed in typed-buffer loads
- HLSL specification: [proposal 0030 DXIL vectors](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0030-dxil-vectors.md)
- Companion blog post: [long-vectors overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/long-vector-non-elementwise-intrinsic.md)

*© 2026 NelCit, CC-BY-4.0.*
