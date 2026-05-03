---
title: "Your groupshared array is bank-conflicting on RDNA"
date: 2026-05-01
author: NelCit
category: workgroup
tags: [hlsl, shaders, performance, workgroup, lds, groupshared]
license: CC-BY-4.0
---

A common shape we see in production compute shaders looks like this:

```hlsl
groupshared float g_Tile[32][32];

[numthreads(32, 1, 1)]
void cs_transpose(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    g_Tile[gi][dtid.x] = SrcBuffer[dtid.y * 32 + dtid.x];
    GroupMemoryBarrierWithGroupSync();
    DstBuffer[dtid.x * 32 + dtid.y] = g_Tile[dtid.x][gi];
}
```

It compiles clean. It produces correct output. Microbenchmarked on an RX 6800
or an RTX 3080, it runs roughly thirty-two times slower on its second LDS
access than the same kernel with one extra float per row of the tile array.
Not 30% slower. 32x slower on that load. The compiler cannot fix it because
the bug is in the index arithmetic, not in the code generator. Every other
rule in the workgroup category of `shader-clippy` is a variant of the same
shape: groupshared memory is a hardware resource with mechanical rules that
are invisible at the source level, and the patterns that violate those
rules look identical to the patterns that don't.

This post is the workgroup-category overview. It covers what groupshared
memory actually is on modern hardware, why bank conflicts cost what they
cost, why barrier semantics in divergent control flow are undefined behaviour
rather than slow, and why almost every per-cell `InterlockedAdd` you have
written should have been a `WaveActiveSum` followed by one atomic.

## What "groupshared" actually is

When you write `groupshared float g_Tile[64]`, you are declaring an
allocation in the GPU's on-chip scratchpad — local data store (LDS) on AMD,
shared memory on NVIDIA, shared local memory (SLM) on Intel. It is not L1
cache; it is a separately addressable SRAM region that lives inside the
compute unit (RDNA CU) or streaming multiprocessor (NVIDIA SM) and is
private to one thread group. On RDNA 2 and RDNA 3 each compute unit has
roughly 64 KB of LDS shared across all in-flight wavefronts on that CU.
On NVIDIA Ada Lovelace each SM provides 100 KB of unified L1 / shared
storage with the partition tunable per kernel. On Intel Xe-HPG each Xe-core
exposes a comparable SLM allocation.

The performance characteristic that matters most is bank parallelism. LDS
on RDNA 2 / 3 is physically 32 banks wide, each bank serving one 32-bit
word per cycle through a per-CU crossbar. NVIDIA shared memory has been
32 banks at 4 bytes each since Volta (Volta+, then Turing, then Ampere,
then Ada, then Blackwell). Intel Xe-HPG SLM is 16 banks at 4 bytes wide.
A wave of 32 lanes that touches 32 distinct banks in the same cycle
completes the access in one clock — full LDS bandwidth. Two lanes that
touch the same bank in the same cycle serialise: the hardware processes
them sequentially, and the rest of the wave waits.

The rule for which addresses land in which bank is simple modular
arithmetic: `bank = (byte_address / 4) mod 32` on RDNA / NVIDIA, `mod 16`
on Xe-HPG. That arithmetic is entirely a function of the index expression
the shader author wrote. The compiler cannot rewrite it; the runtime
cannot detect it. If your indexing collides on a bank, you pay every
cycle the kernel runs.

## The 32x cliff: stride-32 access patterns

The transpose above is the canonical 32-way bank conflict. Lane `i` of the
wave reads `g_Tile[dtid.x][gi]` where `dtid.x = i`, hitting byte offsets
`{0, 32 * 4, 64 * 4, ..., 31 * 32 * 4}`. Mapping each address to a bank
gives `{0, 0, 0, ..., 0}` — every lane on bank 0. The hardware serialises
the access for 32 cycles instead of 1. On a 1080p output target running at
60 Hz, the wave fires roughly two million times per frame; the difference
between a one-cycle access and a thirty-two-cycle access compounds.

The fix is documented in NVIDIA's CUDA C++ Best Practices Guide and has
been standard practice since 2008: pad the inner dimension by one element.
Declare the array `[32][33]` instead of `[32][32]` and the same access
pattern now distributes across all 32 banks, because `33 mod 32 = 1`.
Lane `i` reading `g_Tile[i][j]` now lands at byte offset `(i * 33 + j) * 4`,
which mod 32 equals `(i + j) mod 32` — a uniform spread across the banks.
The cost is one wasted float per row, roughly 0.4-0.8% of the LDS budget;
the win is up to 32x throughput on the cross-stride read. The
[`groupshared-stride-32-bank-conflict`](/rules/groupshared-stride-32-bank-conflict)
rule fires on this exact shape and proposes the +1-padding rewrite.

## The 2x, 4x, 8x, 16x cliffs: non-32 strides

Stride 32 is the worst case. It is not the only case. The conflict factor
for a constant stride `S` accessed by a 32-lane wave is `32 / gcd(S, 32)`:

- Stride 2: 2-way conflict, 2x slowdown
- Stride 4: 4-way conflict, 4x slowdown
- Stride 8: 8-way conflict, 8x slowdown
- Stride 16: 16-way conflict, 16x slowdown
- Stride 64: 32-way conflict (64 mod 32 is 0), 32x slowdown — same as stride 32

The most common production source of partial conflicts is array-of-structures
layout. A `groupshared float g_PerThread[64 * 4]` indexed `g_PerThread[gi * 4 + 0]`
puts every lane on a stride-4 grid: lanes touch banks `{0, 4, 8, 12, 16, 20,
24, 28}`, four lanes per bank. 4-way conflict, 4x slowdown on every read of
field 0. Splitting the layout into structure-of-arrays — `g_Field0[64]`,
`g_Field1[64]`, `g_Field2[64]`, `g_Field3[64]` — makes lane `i` read address
`i`, hitting all 32 banks once. The
[`groupshared-stride-non-32-bank-conflict`](/rules/groupshared-stride-non-32-bank-conflict)
rule reports the conflict factor (2x, 4x, 8x, 16x) so you can prioritise:
a 2-way conflict on a cold path is rarely worth a refactor, a 16-way
conflict on a transpose almost always is.

On Xe-HPG the bank count is 16, so the same modular arithmetic applies with
a different modulus: stride 16 is the 16-way worst case, and stride 2 / 4 / 8
still partially conflict. Both rules accept a `bank-count` option that
defaults to 32; set it to 16 if you target Intel discrete GPUs.

## Barriers in divergent control flow are undefined, not slow

The other half of the workgroup category is barrier hygiene. The rule that
matters most here is
[`barrier-in-divergent-cf`](/rules/barrier-in-divergent-cf), and the
distinction it draws is worth internalising: a barrier inside a divergent
branch is not a performance cliff, it is a hang.

A `GroupMemoryBarrierWithGroupSync` tells the hardware to stall every
thread in the group until all threads check in. The implementation is a
counter — the wave that reaches the barrier increments it, and execution
resumes only when the counter reaches the group size. If some threads
never reach the barrier because they took a divergent branch, the counter
never saturates. On AMD GCN and RDNA, this stalls the entire compute unit;
the scheduler cannot retire the wavefront. On NVIDIA the warp-level
implementation deadlocks similarly. The DX12 runtime and the driver cannot
detect this hang at API level — it manifests as a TDR reset or a
device-removed error in production.

```hlsl
[numthreads(64, 1, 1)]
void cs_barrier_in_branch(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x < 32) {
        GroupMemoryBarrierWithGroupSync();   // only 32 threads arrive — hang
        Sum = (float)dtid.x;
    }
}
```

The non-syncing variants (`GroupMemoryBarrier` without `WithGroupSync`)
exist to flush caches for ordering purposes. Placing them in divergent
control flow does not hang, but it produces incorrect ordering: threads
that skipped the branch may observe stale LDS values. That is a
groupshared data race — undefined behaviour in the D3D12 execution model —
and is harder to debug than a hang because it produces intermittently wrong
output instead of a reliable crash.

The companion rule
[`groupshared-write-then-no-barrier-read`](/rules/groupshared-write-then-no-barrier-read)
catches the inverse hazard: a write to a groupshared cell followed by a
read of a neighbour cell with no intervening barrier. The bug is insidious
because the wave size sometimes aligns with the access pattern accidentally.
A reduction that writes `Sum[gi] = value; if (gi < 32) Sum[gi] += Sum[gi + 32];`
works correctly when the wave size is at least 64 — both writer and reader
are in the same wave and lock-step execution provides accidental ordering —
and breaks when the driver picks wave32 mode. RDNA 2 / 3 supports both
wave32 and wave64; the choice is sometimes the compiler's. Shaders that
"work" on one wave size deadlock on the other after a driver upgrade.

The
[`groupshared-uninitialized-read`](/rules/groupshared-uninitialized-read)
rule covers the related case where the read happens before any thread has
written the cell. LDS is not zero-initialised at thread-group start —
contents are undefined and may contain whatever the previous group on the
CU left there. The correct pattern is always: every thread writes
unconditionally, barrier, every thread reads.

## Atomics on a single cell are wave reductions in disguise

Almost every `InterlockedAdd(g_Counter, 1)` we see in a per-lane branch
should be a `WaveActiveCountBits` followed by one atomic. The
[`groupshared-atomic-replaceable-by-wave`](/rules/groupshared-atomic-replaceable-by-wave)
rule fires on this exact shape:

```hlsl
groupshared uint g_Counter;

[numthreads(64, 1, 1)]
void cs_count_survivors(uint gi : SV_GroupIndex, uint3 dtid : SV_DispatchThreadID) {
    bool alive = SrcBuffer[dtid.x] > 0;
    if (alive) {
        InterlockedAdd(g_Counter, 1);   // 64 lanes serialise on one address
    }
}
```

LDS atomics on every modern GPU serialise on the cell address. When 32
lanes (wave32) or 64 lanes (RDNA wave64) contend on the same
`InterlockedAdd` site, the hardware processes them sequentially: 32 round
trips through the AMD LDS atomic ALU, 32 through NVIDIA's shared-memory
atomic unit, similar on Xe-HPG. Even with hardware coalescing of monotonic
adds on some IHVs, the worst case is 32x the single-atomic latency, and
the atomic unit is single-issue per cycle, so it stalls every other LDS
access in the same wave for the duration.

The wave-reduce idiom collapses this to one round trip. `WaveActiveSum`
runs in a handful of cycles using cross-lane DPP / shfl hardware
(single-issue, one or two cycles to reduce 32 lanes on RDNA 3 / Ada /
Xe-HPG); only one lane then issues the atomic, contributing the entire
wave's sum:

```hlsl
uint wave_sum = WaveActiveCountBits(alive);
if (WaveIsFirstLane()) {
    InterlockedAdd(g_Counter, wave_sum);
}
```

The end result on the LDS counter is identical, but the LDS atomic unit
is freed for other waves, the per-lane atomic latency is gone, and the
LDS bandwidth occupied by 32 atomic transactions collapses to one. For
OR / AND / XOR / MIN / MAX, the corresponding wave reductions
(`WaveActiveBitOr`, `WaveActiveBitAnd`, `WaveActiveBitXor`,
`WaveActiveMin`, `WaveActiveMax`) supply the wave-folded operand. The
pattern matters most in stream-compaction kernels (count survivors),
small histogram bins, and visibility-buffer accumulators.

## The smaller traps: volatile, dead stores, double writes

Three more rules round out the workgroup category and deserve a brief
mention because they correspond to widespread patterns we see in real
shader codebases.

[`groupshared-volatile`](/rules/groupshared-volatile) catches the cargo-cult
import of CPU-side `volatile` semantics. HLSL's memory model treats
`volatile` as a hint to suppress *intra-thread* reordering; it says
nothing about cross-thread visibility, which is the entire reason
`groupshared` exists. Programmers reach for `volatile` when they want
"every thread sees the latest value" and get the per-thread reordering
fence they did not need plus zero cross-thread synchronisation. Worse,
the qualifier defeats the LDS coalescer on RDNA and the shared-memory
optimiser on NVIDIA, costing real bandwidth in exchange for nothing.
The fix is mechanical: drop `volatile`, then ask whether the surrounding
code needs a `GroupMemoryBarrierWithGroupSync` instead.

[`groupshared-overwrite-before-barrier`](/rules/groupshared-overwrite-before-barrier)
catches `gs[gi] = init_value; gs[gi] = computed_value;` in straight-line
code with no intervening barrier. The first write is invisible to every
other thread because LDS visibility is barrier-relative — a write becomes
observable only after a `GroupMemoryBarrier*`. With no barrier, the
second write supersedes the first in the LDS bank and no consumer wave
ever sees the original value. Refactoring is the usual cause: the first
write was useful when there used to be a barrier between the two stores,
and became dead when the barrier moved.

[`groupshared-dead-store`](/rules/groupshared-dead-store) is the broader
form. A write to a groupshared cell where no read of that cell is reached
on any CFG path costs an LDS write port for one cycle and keeps the array
allocation live for occupancy budgeting — a 16 KB groupshared array is
budgeted at 16 KB regardless of whether anyone observes its writes. The
rule shares the def-use scan that `unused-cbuffer-field` needs (per ADR
0011) and is conservative: any indexed read whose index cannot be
statically resolved is treated as a possible read of every cell, so the
rule fires only when no surviving read can reach the store.

## What to take away

The workgroup category is a coherent set of rules around one resource:
the on-chip scratchpad. The performance rules
([`groupshared-stride-32-bank-conflict`](/rules/groupshared-stride-32-bank-conflict),
[`groupshared-stride-non-32-bank-conflict`](/rules/groupshared-stride-non-32-bank-conflict),
[`groupshared-atomic-replaceable-by-wave`](/rules/groupshared-atomic-replaceable-by-wave))
are about respecting bank parallelism and avoiding serialised primitives
where wave-collective ones exist. The correctness rules
([`barrier-in-divergent-cf`](/rules/barrier-in-divergent-cf),
[`groupshared-write-then-no-barrier-read`](/rules/groupshared-write-then-no-barrier-read),
[`groupshared-uninitialized-read`](/rules/groupshared-uninitialized-read),
[`groupshared-volatile`](/rules/groupshared-volatile)) are about
respecting the barrier-relative memory model that LDS uses. The hygiene
rules
([`groupshared-overwrite-before-barrier`](/rules/groupshared-overwrite-before-barrier),
[`groupshared-dead-store`](/rules/groupshared-dead-store)) recover LDS
budget and clarify producer-consumer reasoning so the rest of the
analysis works.

If you have a compute shader that has not been profiled in a while, the
single highest-leverage thing to inspect is the indexing into every
`groupshared` array. The bank arithmetic is `(byte_address / 4) mod 32`
on RDNA and NVIDIA, `mod 16` on Xe-HPG. If your stride shares a non-trivial
GCD with the bank count, you are paying for every wave that touches it.

Browse the full set of workgroup rules at
[/rules/?category=workgroup](/rules/?category=workgroup) and run
`shader-clippy lint` over your compute pass. The `--fix` flag will apply
the machine-applicable rewrites (the `groupshared-volatile` removal
notably). The rest carry diagnostic notes that explain the GPU
mechanism and point at the specific index expression or barrier site
that triggered the rule.

---

`shader-clippy` is an open-source HLSL + Slang linter. Rules, issues, and
discussion live at
[github.com/NelCit/shader-clippy](https://github.com/NelCit/shader-clippy).
If you have encountered a shader pattern that should be a lint rule,
open an issue.

---

*© 2026 NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
