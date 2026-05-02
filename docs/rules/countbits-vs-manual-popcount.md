---
id: countbits-vs-manual-popcount
category: math
severity: warn
applicability: suggestion
since-version: v0.2.0
phase: 2
---

# countbits-vs-manual-popcount

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

Loops or expression trees that count the set bits of an integer scalar by hand, in any of the canonical forms: a `for`/`while` loop that shifts and accumulates the low bit (`while (x) { count += x & 1; x >>= 1; }`), Brian Kernighan's clear-the-lowest-bit loop (`while (x) { x &= x - 1; ++count; }`), an unrolled SWAR-style sequence of mask-shift-add reductions (`x = (x & 0x55555555u) + ((x >> 1) & 0x55555555u);` and the matching widening passes), or a small lookup-table indexed by an 8- or 16-bit slice. The rule keys on the structural shape — a loop body that strictly tests, masks, and decrements a working integer, or a sequence of three or four magic-constant masks at 0x55555555/0x33333333/0x0F0F0F0F. It does not fire when the loop body does anything besides bit counting (e.g., remembers which bits were set, or maps each bit to a side effect).

## Why it matters on a GPU

Every shader-capable GPU shipping since DirectX 11 has a single-cycle population-count instruction at the ISA level, and HLSL exposes it as `countbits(uint)`. On AMD RDNA 3 it lowers to `v_bcnt_u32_b32`, a full-rate VALU op that issues at one result per SIMD32 lane per clock. NVIDIA Turing and Ada Lovelace expose `POPC` as a single SASS instruction with similar throughput on the integer pipe. Intel Xe-HPG provides `cbit` as a one-cycle integer ALU op. Replacing a 32-iteration loop with one of these instructions is not a small win: it is a 32x reduction in dynamic instruction count for the worst-case input and an unbounded reduction in branch-divergence overhead, because the loop's exit condition depends on the popcount value and therefore varies across the wave.

The Kernighan variant looks cleaner — it iterates only as many times as there are set bits — but on a wave-wide SIMD machine the iteration count of every active lane is the maximum over the wave. A wave with one lane holding `0xFFFFFFFFu` runs 32 iterations everywhere; the other 31 lanes spin executing masked nops while one lane finishes. The same divergence cost falls on the shift-and-add form. SWAR popcount avoids the loop entirely and is the conventional manual rewrite, but it still costs roughly 12 VALU instructions (four mask-and-shift-add passes plus a final multiply-shift) on RDNA 3 and similar on NVIDIA — twelve cycles where `countbits` would take one. Lookup-table popcounts are worse still on a GPU because the table lives in either a constant buffer (one cbuffer fetch per byte processed, with bandwidth contention) or LDS/shared memory (LDS bank conflicts when multiple lanes index different rows of the same bank).

The rule applies anywhere bit masks describe set membership: bone-influence weight masks in skinning, lane-mask reductions in wave intrinsic emulation, primitive-visibility bitsets in mesh-shader culling, and material-feature flag tests in uber-shaders. In a mesh-shader meshlet, `countbits` over the active-primitive mask is the canonical way to compute the output primitive count for `SetMeshOutputCounts`; doing it by hand would burn budget that the meshlet pipeline cannot spare.

## Examples

### Bad

```hlsl
// Shift-and-add loop. Worst case 32 iterations, branch divergence across the wave.
uint manual_popcount(uint x) {
    uint count = 0;
    while (x != 0) {
        count += x & 1u;
        x >>= 1;
    }
    return count;
}

// SWAR variant — branchless but ~12 VALU instructions.
uint swar_popcount(uint x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}
```

### Good

```hlsl
// One ISA-level instruction on every modern GPU.
uint popcount(uint x) {
    return countbits(x);
}
```

## Options

none

## Fix availability

**suggestion** — A candidate rewrite to `countbits()` is shown but not auto-applied. The lint target may have intentional non-popcount semantics (e.g., the loop body also records bit positions, or the SWAR pattern is part of a broader bit-twiddling sequence the tool has not fully matched). Authors should confirm the loop or expression genuinely returns only the set-bit count before accepting the fix.

## See also

- Related rule: [firstbit-vs-log2-trick](firstbit-vs-log2-trick.md) — replaces `log2((float)x)` MSB tricks with `firstbithigh()`
- Related rule: [manual-f32tof16](manual-f32tof16.md) — another manual bit-manipulation pattern with an intrinsic equivalent
- HLSL intrinsic reference: `countbits` in the DirectX HLSL Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/countbits-vs-manual-popcount.md)

*© 2026 NelCit, CC-BY-4.0.*
