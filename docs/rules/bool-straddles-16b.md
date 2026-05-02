---
id: bool-straddles-16b
category: bindings
severity: error
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# bool-straddles-16b

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `bool` member inside a `cbuffer` or `ConstantBuffer<T>` whose byte offset causes it to span — or be placed at — a 16-byte register boundary. Under HLSL packing rules a `bool` occupies 4 bytes (one DWORD, promoted from 1-bit to 32-bit for GPU register layout), but the packing rule that prevents members from straddling a 16-byte slot boundary can place the `bool` at byte offset 12 inside a slot, leaving it exactly at the boundary. The rule uses Slang's reflection API to retrieve each member's byte offset and size, then checks whether `(offset % 16) + sizeof(member) > 16`. The canonical trigger is `float3` (12 bytes) followed by `bool`: `float3` lands at an aligned offset, consumes 12 bytes, and the `bool` at offset 12 within the slot technically fits in the last 4 bytes — but whether the compiler packs it there or bumps it to the next slot is implementation-defined and varies across `dxc`, `fxc`, and Slang. See `tests/fixtures/phase3/bindings.hlsl`, lines 12–18 for the `StraddleCB` example.

## Why it matters on a GPU

A `bool` in a cbuffer is loaded as part of a 16-byte slot fetch. When the `bool` sits at bytes 12–15 of a slot, the load is well-defined and correct — the problem is that the D3D12 / HLSL specification treats the effective value as the result of a non-zero test on the underlying DWORD, but the exact byte layout of that DWORD relative to the 16-byte register depends on whether the compiler chose to pack it into the same slot or bumped it to the next one. `fxc` and `dxc` have historically made different decisions for this layout, meaning a cbuffer filled on the CPU side using one compiler's reflection output may be read incorrectly by a shader compiled with another. This is a correctness hazard, not merely a performance issue.

On NVIDIA hardware the constant-data path loads 128-bit (16-byte) words from the constant cache into uniform registers. If the `bool` is bumped to the next 16-byte slot by the compiler, the CPU-side fill code (which mirrors the layout using `offsetof` from a C++ struct, or `dxc` reflection queries) must agree exactly. Any disagreement produces a shader that reads a stale or zero value for the flag, with no GPU-side error or validation layer warning in release builds. The result is typically a rendering artefact (e.g., a conditional lighting path that never fires, or always fires) that is extremely difficult to trace back to a layout mismatch.

Beyond the cross-compiler hazard, a `bool` at offset 12 within a 16-byte slot breaks the mental model of "single-load semantics": a developer reading the struct expects that `float3 Tint; bool UseTint;` is a natural 16-byte grouping, but the actual load may require two slot fetches if the compiler places `UseTint` in slot 1 rather than slot 0. The fix is explicit: either insert a `float` pad member before the `bool` to align it to the next slot boundary, or reorder the struct so the `bool` appears after all same-slot scalars.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 12-18
// HIT(bool-straddles-16b): Tint (12B) + bool at offset 12 within a 16-byte slot.
// Layout is implementation-defined: dxc may pack UseTint into slot 0 bytes 12-15,
// fxc may bump it to slot 1 byte 0-3.
cbuffer StraddleCB : register(b1) {
    float3 Tint;     // offset 0, size 12
    bool   UseTint;  // offset 12 — straddles or abuts the 16-byte boundary
    float4 More;     // offset 16 (or 32 if UseTint was bumped)
};
```

### Good

```hlsl
// Option A: pad to push bool to the next slot start.
cbuffer StraddleCB_Fixed_A : register(b1) {
    float3 Tint;      // offset 0,  size 12
    float  _pad0;     // offset 12, size 4  — explicit padding
    bool   UseTint;   // offset 16, size 4  — starts a new slot cleanly
    float4 More;      // offset 32
};

// Option B: reorder — place bool after all float4 members.
cbuffer StraddleCB_Fixed_B : register(b1) {
    float4 More;     // offset 0,  size 16
    float3 Tint;     // offset 16, size 12
    bool   UseTint;  // offset 28, size 4  — bytes 12-15 of slot 1 (no straddle)
};
```

## Options

none

## Fix availability

**suggestion** — Resolving the straddle requires inserting explicit padding or reordering members, both of which may break CPU-side layout assumptions. `hlsl-clippy fix` generates a candidate layout with explicit `packoffset` annotations but does not apply it automatically.

## See also

- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — general alignment gaps in cbuffer layouts
- Related rule: [oversized-cbuffer](oversized-cbuffer.md) — cbuffer exceeds the default 4 KB threshold
- HLSL constant buffer packing rules: `packoffset` keyword and `bool` layout in the DirectX HLSL reference
- D3D12 Root Signature specification: constant buffer view descriptor layout
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/bool-straddles-16b.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
