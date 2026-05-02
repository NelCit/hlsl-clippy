---
id: oversized-cbuffer
category: bindings
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
---

# oversized-cbuffer

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `cbuffer` or `ConstantBuffer<T>` whose total byte size, as reported by Slang's reflection API, exceeds a configurable threshold (default: 4096 bytes, i.e., 4 KB). The rule fires on the declaration itself, naming the actual size and the threshold. The canonical trigger in the fixture is `cbuffer Huge` with a `float4[256]` member (4096 bytes) plus a `float4 Tail` (16 bytes), totalling 4112 bytes (see `tests/fixtures/phase3/bindings.hlsl`, lines 27–31).

## Why it matters on a GPU

Every cbuffer bound to a shader occupies space in the GPU's constant-data path. On AMD RDNA/RDNA 2/RDNA 3, constant data flows through the scalar register file (SGPRs) and the associated scalar data cache (K-cache, 16 KB per CU shared across all waves on that CU). On NVIDIA Turing and Ada Lovelace, cbuffer contents live in a dedicated constant-data L1 cache. In both cases, a large cbuffer that exceeds the effective cache capacity forces evictions: data loaded by one wave is evicted before adjacent waves can reuse it, turning what should be a broadcast-per-CU operation into repeated cache-miss fetches from L2 or VRAM.

The D3D12 specification constrains constant buffer views to at most 65536 bytes (64 KB), but the hardware-efficient range is much smaller. A cbuffer that fits within one or two cache lines (64–128 bytes) will be resident in L1 across the entire lifetime of a wavefront with no eviction risk. A cbuffer approaching 4 KB risks partial eviction on RDNA even for moderately wave-parallel dispatches. A cbuffer over 4 KB almost certainly exceeds the useful constant-cache working set for any single dispatch, and data that is not actually accessed on a given code path is loaded and cached unnecessarily.

Common causes of oversized cbuffers: accumulating per-material, per-frame, and per-draw constants into one "mega-cbuffer" to minimise descriptor-table binding cost, or storing large look-up tables (e.g., `float4[64]` coefficient arrays) that would be better served by a `StructuredBuffer` or `Texture1D` on the read path. The diagnostic provides the actual size so the author can decide whether to split the cbuffer, move large arrays to structured buffers, or raise the threshold.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase3/bindings.hlsl, lines 27-31
// HIT(oversized-cbuffer): 4096 + 16 = 4112 bytes — above the 4096-byte default threshold.
cbuffer Huge : register(b3) {
    float4 BigArray[256];  // 4096 bytes
    float4 Tail;           //   16 bytes — total 4112 bytes
};
```

### Good

```hlsl
// Split: move the large array to a StructuredBuffer (accessed via the read path,
// cached by texture / L2 rather than constant cache).
StructuredBuffer<float4> BigData : register(t10);

cbuffer HugeFixed : register(b3) {
    float4 Tail;           // 16 bytes — well within threshold
    uint   BigDataCount;   // 4 bytes
};
```

## Options

- `threshold-bytes` (integer, default: `4096`) — fire when the cbuffer's total byte size exceeds this value. Set higher (e.g., `8192`) to tolerate larger cbuffers in projects that deliberately use a mega-cbuffer pattern, or lower (e.g., `256`) to enforce tighter constant budgets.

  Example in `.hlsl-clippy.toml`:
  ```toml
  [rules.oversized-cbuffer]
  threshold-bytes = 2048
  ```

## Fix availability

**suggestion** — Reducing cbuffer size typically requires moving data to a different resource type (e.g., `StructuredBuffer`, `Texture1D`) or splitting the cbuffer across multiple bind points. These changes cross file and API boundaries and require human verification. `hlsl-clippy fix` does not apply a fix automatically.

## See also

- Related rule: [cbuffer-padding-hole](cbuffer-padding-hole.md) — alignment gaps that inflate the cbuffer size unnecessarily
- Related rule: [cbuffer-fits-rootconstants](cbuffer-fits-rootconstants.md) — small cbuffers that could be root constants on D3D12
- Related rule: [cbuffer-divergent-index](cbuffer-divergent-index.md) — cbuffer read with a divergent index (serializes on NVIDIA constant cache)
- D3D12 Root Signature specification: constant buffer view size constraints
- Companion blog post: [bindings overview](../blog/bindings-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/oversized-cbuffer.md)

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
