---
id: scratch-from-dynamic-indexing
category: memory
severity: warn
applicability: none
since-version: v0.7.0
phase: 7
language_applicability: ["hlsl", "slang"]
---

# scratch-from-dynamic-indexing

> **Status:** shipped (Phase 7) -- see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A local array declared inside a function body — `float4 lut[N]` or similar —
that is indexed with a non-constant expression: a cbuffer field, an
`SV_GroupIndex`, or any value whose value is unknown at compile time. The rule
fires when the compiler cannot statically determine which array element is
accessed, forcing the array to be allocated as an indexable temporary (scratch
memory) rather than being scalarised into individual registers.

## Why it matters on a GPU

On AMD RDNA and NVIDIA Turing-class hardware, the register file is not
randomly addressable by lane-computed indices at the instruction level.
A register instruction must name its source and destination operands at
compile time; there is no "register indirect" addressing mode in the ISA.
When the HLSL compiler encounters a local array indexed by a runtime value it
cannot eliminate, it maps the array to scratch memory: a per-lane stack
allocation that lives in the VRAM L2 or GDDR cache hierarchy, not in the
on-chip register file. On RDNA 3, scratch-memory bandwidth to L2 runs at
roughly 1.28 TB/s aggregate, but per-thread latency for a random-access
scratch read is 80-300 cycles — compared to roughly 4 cycles for a register
read. A shader that touches a scratch array in its inner loop will stall
waiting for off-chip data even though the array is logically small.

The DXIL intermediate representation makes this visible: a dynamically-indexed
local array appears as `dx.op.rawBufferLoad`/`rawBufferStore` instructions
into a per-invocation VRAM buffer, whereas a statically-resolved array
scalarises to named SSA values. The compile-time cost of the scratch path also
includes an additional address computation per access. On compute shaders with
high occupancy, the per-wave scratch allocation compounds: each wave requires
its own private scratch region, and a large scratch requirement can exhaust the
available per-CU allocation and reduce the number of concurrent waves.

The correct fix is to replace the runtime-indexed local array with one of:
a `Texture1D` or `Buffer` lookup (L1-cached, 4-8 cycle hit latency), a
`StructuredBuffer` with the index as the read coordinate, or an explicit
`switch`/series-of-`if` that the compiler can scalarise, if the number of
cases is small. If the data is truly uniform across all lanes, a cbuffer array
accessed with a uniform index will be scalarised by the compiler.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase7/register_pressure.hlsl — HIT(scratch-from-dynamic-indexing)
float4 ps_dynamic_array_index(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 lut[8];
    lut[0] = float4(1, 0, 0, 1);
    // ... (8 entries)
    // DynIdx is a cbuffer field — unknown at compile time.
    return lut[DynIdx & 7u] * Exposure;
}
```

### Good

```hlsl
// Move the LUT to a cbuffer array (uniform index → scalarised by compiler).
cbuffer LutCB {
    float4 lut[8];
};
float4 ps_cbuffer_lut(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return lut[DynIdx & 7u] * Exposure;
}

// Or use a Buffer<float4> for non-uniform per-lane indexing (L1-cached).
Buffer<float4> LutBuffer : register(t3);
float4 ps_buffer_lut(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return LutBuffer[DynIdx & 7u] * Exposure;
}

// Static index — stays in registers, no scratch.
float4 ps_static_array_index(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 lut[4] = { float4(1,0,0,1), float4(0,1,0,1), float4(0,0,1,1), float4(1,1,1,1) };
    return lut[2] * Exposure;   // constant index — stays in registers
}
```

## Options

none — no configurable thresholds. To silence on a specific array, use inline
suppression:

```hlsl
// shader-clippy: allow(scratch-from-dynamic-indexing)
return lut[DynIdx & 7u] * Exposure;
```

## Fix availability

**none** — Choosing the right replacement (cbuffer array vs. `Buffer<T>` vs.
`switch`) depends on whether the index is uniform or per-lane, and on the
array size and access frequency. The rule reports the problem; the fix is
always manual.

## See also

- Related rule: [vgpr-pressure-warning](vgpr-pressure-warning.md) — scratch
  usage directly inflates effective register pressure
- HLSL intrinsic reference: `Buffer<T>`, `StructuredBuffer<T>`,
  `cbuffer` arrays — alternatives to local indexable arrays
- Companion blog post: [memory overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/scratch-from-dynamic-indexing.md)

---

_Documentation is licensed under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)._
