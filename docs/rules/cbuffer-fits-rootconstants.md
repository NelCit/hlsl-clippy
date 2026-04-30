---
id: cbuffer-fits-rootconstants
category: bindings
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
---

# cbuffer-fits-rootconstants

> **Status:** pre-v0 — rule scheduled for Phase 3; see [ROADMAP](../../ROADMAP.md).

## What it detects

A `cbuffer` or `ConstantBuffer<T>` whose total size is at most 32 bytes (8 DWORDs) — the maximum number of 32-bit values that D3D12 root constants can hold in one root parameter slot. The rule uses Slang's reflection API to determine the cbuffer's total byte size and fires when `total_bytes <= 32`. Both fixture examples qualify: `cbuffer Tiny` (8 bytes, 2 DWORDs; see `tests/fixtures/phase3/bindings.hlsl`, lines 21–24) and the four-DWORD `cbuffer PushCB` and the two-DWORD `cbuffer TinyBlurCB` (see `tests/fixtures/phase3/bindings_extra.hlsl`, lines 8–19).

## Why it matters on a GPU

On D3D12, a cbuffer bound via a descriptor heap or root descriptor requires an indirection: the GPU reads a pointer from the root signature, fetches the constant-buffer view descriptor, and then loads the cbuffer data from the address the descriptor names. This is two dependent memory reads before the first shader data arrives. Root constants, by contrast, live directly in the command list's root signature data: the driver uploads them into the root-argument area at `ExecuteCommandLists` time, and the GPU reads them from a small root-argument register block that is broadcast to all invocations of the dispatch or draw without any descriptor-table indirection.

The practical effect is one fewer dependent load on the constant-data path per shader invocation. On high-throughput dispatches — a compute shader invoked 1920×1080 / 64 = ~32 400 times per frame — the constant-data path is exercised once per wavefront. Even if the cbuffer data is L1-resident, the descriptor indirection adds latency on the critical path of wave launch. Root constants bypass this entirely: the data is forwarded from the root-argument block to the scalar register file at wave launch time with no descriptor lookup.

The D3D12 root signature allows up to 64 DWORDs total across all root parameters, and each DWORD in the root signature costs GPU cycles to manage (root argument upload, validation, broadcasting). A cbuffer of 8 DWORDs or fewer almost always represents per-draw or per-pass data (draw index, material ID, pass flags) — exactly the use case D3D12 root constants are designed for. Migrating these small cbuffers to root constants removes a descriptor-heap allocation, eliminates one descriptor-table slot, reduces the descriptor-heap fragmentation risk, and removes the indirection from the hot constant-data path.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 21-24
// HIT(cbuffer-fits-rootconstants): 8 bytes total — fits in 2 root constant DWORDs.
cbuffer Tiny : register(b2) {
    uint InstanceID;   // 4 bytes
    uint MaterialID;   // 4 bytes
};

// From tests/fixtures/phase3/bindings_extra.hlsl, lines 8-13
// HIT(cbuffer-fits-rootconstants): 4 DWORDs = 16 bytes.
cbuffer PushCB {
    uint  DrawId;
    uint  MeshletOffset;
    uint  InstanceCount;
    float Time;
};
```

### Good

```hlsl
// D3D12 root signature declaration (C++ API side — not HLSL):
//   CD3DX12_ROOT_PARAMETER1 params[1];
//   params[0].InitAsConstants(2, 0, 0); // 2 DWORDs, b0, space0
//   ...
//   cmdList->SetGraphicsRoot32BitConstants(0, 2, &pushData, 0);

// Corresponding HLSL — cbuffer replaced by root constants.
// No cbuffer declaration needed; the fields are accessed via a struct.
struct PushConstants {
    uint InstanceID;
    uint MaterialID;
};
ConstantBuffer<PushConstants> Push : register(b2, space0);
// With a proper root-constant root parameter bound at b2, this reads
// directly from the root-argument area with no descriptor indirection.
```

## Options

none

## Fix availability

**suggestion** — Migrating a cbuffer to root constants requires changes to the D3D12 root signature on the CPU side (`InitAsConstants`) and to the `ExecuteCommandLists` call sequence. The HLSL shader side change is minimal (optionally replacing the `cbuffer` declaration with a `ConstantBuffer<T>` or leaving it as-is if the root parameter binding matches). `hlsl-clippy fix` generates the suggested root-signature snippet as a comment but does not modify HLSL source automatically.

## See also

- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — alignment gaps that may inflate the cbuffer beyond the root-constant limit
- Related rule: [oversized-cbuffer](oversized-cbuffer.md) — cbuffer exceeds the default 4 KB threshold
- D3D12 specification: Root Signature Version 1.1, `D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS`
- DirectX-Specs: root signature cost model (64 DWORD total budget)
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/cbuffer-fits-rootconstants.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
