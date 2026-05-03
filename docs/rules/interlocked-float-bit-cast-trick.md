---
id: interlocked-float-bit-cast-trick
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# interlocked-float-bit-cast-trick

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

The hand-rolled idiom for atomic min/max on floating-point data: a sequence of `asuint(x)`, conditional sign-bit flip (`x ^ 0x80000000` for negatives, `~x` for the inverted-comparison trick), `InterlockedMin` / `InterlockedMax` on the resulting `uint`, and a final `asfloat` to recover the value. The rule fires when the surrounding shader is compiled for SM 6.6 or higher, where `InterlockedMin(float, float)` and `InterlockedMax(float, float)` are native intrinsics and the bit-cast dance is no longer necessary.

## Why it matters on a GPU

Atomic min/max on floats is needed in several common workloads: depth-of-field min-Z buffers, soft-shadow blocker depth reductions, voxel-cone tracing min-distance writes, and TAA history-clip range computations. Before SM 6.6, HLSL had no atomic float intrinsic, so shaders cast the float to `uint`, exploited the property that IEEE 754 single-precision floats compare correctly as signed magnitudes when sign-flipped, did the integer atomic, then cast back. The trick is correct, but it expands to roughly 8-12 instructions per atomic call site (sign extraction, conditional XOR, integer atomic, inverse sign restoration, `asfloat`), and requires the shader author to remember the sign convention for their value range — a frequent source of subtle off-by-sign bugs in negative-depth or negative-LOD code paths.

SM 6.6 added native float atomics. On AMD RDNA 3, the new instructions `ds_min_f32`, `ds_max_f32`, `image_atomic_fmin`, `image_atomic_fmax` execute in one LDS or texture-atomic slot, replacing the multi-instruction software dance. On NVIDIA Ada and newer, the `ATOMS.FMIN` / `ATOMS.FMAX` family for shared memory and the `ATOM.FADD.F32` / `ATOM.FMIN.F32` / `ATOM.FMAX.F32` family for global memory provide the same single-instruction fast path. On Intel Xe-HPG, the LSC pipe exposes `LSC_ATOMIC_FMIN` / `LSC_ATOMIC_FMAX`. In all three cases, the native form is at least 3-5x faster than the bit-cast software emulation per atomic site, and produces noticeably smaller binaries — DXIL size for a typical depth-of-field minimum-depth pass drops by 10-20%.

The bit-cast pattern also has a correctness footgun the native form sidesteps. The standard sign-flip trick (`if (asint(x) < 0) bits = ~bits else bits = bits ^ 0x80000000`) requires that the input is not NaN — `asuint(NaN)` after the conditional flip can land in a region of integer space that no real float maps to, breaking the round-trip. Native `InterlockedMin/Max(float)` define NaN behaviour explicitly per IEEE 754-2019 minNum / maxNum semantics, so the shader does not need to defend against NaN inputs at every atomic site.

## Examples

### Bad

```hlsl
RWStructuredBuffer<uint> g_MinDepthBits : register(u0);

void update_min_depth(uint slot, float depth) {
    // Pre-SM 6.6 idiom: sign-flip the bits so signed-int compare matches
    // float order, then InterlockedMin on uint, then cast back.
    uint bits = asuint(depth);
    bits = (bits & 0x80000000u) ? ~bits : (bits ^ 0x80000000u);
    InterlockedMin(g_MinDepthBits[slot], bits);
}
```

### Good

```hlsl
// SM 6.6+: native float atomic. One instruction, NaN semantics defined,
// no sign-bit gymnastics.
RWStructuredBuffer<float> g_MinDepth : register(u0);

void update_min_depth_native(uint slot, float depth) {
    InterlockedMin(g_MinDepth[slot], depth);
}
```

## Options

- `min-shader-model` (string, default: `"6.6"`) — only fire when the compiled shader model is at least this version. Set to `"6.7"` or higher to delay the recommendation if the deployment target is older.

## Fix availability

**suggestion** — The textual rewrite is mechanical, but it requires changing the buffer's element type from `uint` to `float`, which propagates to the resource declaration, the C++/D3D12 buffer creation site, and any other shaders that read the same buffer. The diagnostic identifies the bit-cast pattern and recommends the native intrinsic with a note that the resource type must be updated.

## See also

- Related rule: [interlocked-bin-without-wave-prereduce](interlocked-bin-without-wave-prereduce.md) — wave pre-reduction for atomic-heavy workloads
- Related rule: [groupshared-stride-32-bank-conflict](groupshared-stride-32-bank-conflict.md) — groupshared bank conflicts
- HLSL intrinsic reference: `InterlockedMin`, `InterlockedMax`, `asfloat`, `asuint` in the DirectX HLSL Intrinsics documentation
- Microsoft DirectX docs: SM 6.6 Atomic64 and Float Atomics
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/interlocked-float-bit-cast-trick.md)

*© 2026 NelCit, CC-BY-4.0.*
