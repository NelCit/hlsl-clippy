---
id: long-vector-in-cbuffer-or-signature
category: long-vectors
severity: error
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# long-vector-in-cbuffer-or-signature

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `vector<T, N>` with `N >= 5` declared as a member of a `cbuffer` / `ConstantBuffer<T>`, or as the type of a vertex / pixel / hull / domain / geometry IO signature element. The SM 6.9 long-vector specification (DXIL vectors, proposal 0030) restricts long vectors to in-shader compute use; cbuffer layout and stage-IO signatures are explicitly out of scope. Slang reflection identifies the binding kind and signature stage; the rule fires on the first long-vector member encountered at one of those boundaries.

## Why it matters on a GPU

The cbuffer packing rules and the inter-stage IO packing rules predate the long-vector feature. Cbuffer layout is fixed by HLSL packing (16-byte slots, `float4`-shaped vector members, scalar tail packing), and the runtime maps cbuffer fetches onto the IHV-specific scalar / constant-data path. Inter-stage IO is packed into per-vertex slots (NVIDIA Ada: 32 four-component slots; AMD RDNA 2/3: parameter-cache entries; Intel Xe-HPG: URB-style slots); the slot allocation is fixed at PSO compile time and assumes 1/2/3/4-wide vector types only.

Long vectors break both surfaces: a `vector<float, 8>` cbuffer member has no defined `packoffset` because the member crosses two 16-byte slots in a way the packing rules don't enumerate, and a `vector<float, 8>` IO signature element doesn't fit any IHV's per-vertex slot. The DXC validator catches both forms and emits a precise error; the lint replaces the validator round trip with a source-located diagnostic and points the author at the right shape.

The fix is to split the long vector into chunks that respect the packing rules: an array of `float4` for cbuffer storage, multiple TEXCOORD slots for IO signature. The rule emits a candidate rewrite as a comment so the author can review.

## Examples

### Bad

```hlsl
// Long vector in cbuffer — packing undefined.
cbuffer Frame : register(b0) {
    vector<float, 8> Weights;   // ERROR: long vector at cbuffer boundary
};

// Long vector as vertex output — IO signature does not pack.
struct VSOut {
    float4           pos     : SV_Position;
    vector<float, 8> attribs : TEXCOORD0;  // ERROR: long vector in IO sig
};
```

### Good

```hlsl
// Cbuffer: split into float4 array.
cbuffer Frame : register(b0) {
    float4 Weights[2];
};

// IO signature: split into multiple float4 slots.
struct VSOut {
    float4 pos      : SV_Position;
    float4 attrib0  : TEXCOORD0;
    float4 attrib1  : TEXCOORD1;
};
```

## Options

none

## Fix availability

**suggestion** — Splitting the long vector requires updating every reader to use the new layout, which the rule cannot do safely without a full project view. The diagnostic emits the candidate split as a comment.

## See also

- Related rule: [long-vector-non-elementwise-intrinsic](long-vector-non-elementwise-intrinsic.md) — companion long-vector spec rule
- Related rule: [long-vector-typed-buffer-load](long-vector-typed-buffer-load.md) — companion long-vector spec rule
- Related rule: [excess-interpolators](excess-interpolators.md) — IO signature pressure
- HLSL specification: [proposal 0030 DXIL vectors](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0030-dxil-vectors.md)
- Companion blog post: [long-vectors overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/long-vector-in-cbuffer-or-signature.md)

*© 2026 NelCit, CC-BY-4.0.*
