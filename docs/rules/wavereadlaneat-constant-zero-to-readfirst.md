---
id: wavereadlaneat-constant-zero-to-readfirst
category: math
severity: warn
applicability: machine-applicable
since-version: v0.2.0
phase: 2
---

# wavereadlaneat-constant-zero-to-readfirst

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

Calls of the form `WaveReadLaneAt(x, 0)` where the second argument is the integer literal `0` (or a `static const` integer that folds to zero at parse time). The match is on the literal lane index, not on the value of `x`. The rule does not fire on `WaveReadLaneAt(x, K)` for any non-zero compile-time constant `K` (that case has its own portability concern when wave size is not pinned and is queued as a Phase 3 reflection-aware rule, `wavereadlaneat-constant-non-zero-portability`), and does not fire on `WaveReadLaneAt(x, dynamic)` calls where the lane index is a runtime expression.

## Why it matters on a GPU

`WaveReadLaneAt(x, lane)` is the general lane-index broadcast: every active lane in the wave receives the value of `x` from the lane numbered `lane`. The hardware contract is general-purpose — `lane` may be any value in `[0, WaveGetLaneCount())`, may differ across waves, and is required to be uniform across the active lanes for the result to be defined. Because `lane` is a runtime input from the compiler's perspective, the lowering must emit the lane-index broadcast in its general form: on AMD RDNA 2/3 that is `v_readlane_b32` (a SALU instruction) plus the bookkeeping required to broadcast back to VGPRs; on NVIDIA Turing and Ada Lovelace, the `SHFL.IDX` shuffle with a per-lane index argument; on Intel Xe-HPG, the SLM-backed lane-index path. None of these are expensive in absolute terms, but they all encode "the lane index is variable" and the compiler cannot strip the index plumbing.

`WaveReadLaneFirst(x)` is the specialisation: broadcast the value of `x` from the first active lane, with no lane-index argument at all. On RDNA the lowering is `v_readfirstlane_b32`, an SALU instruction with one source operand and no lane-index encoding — strictly cheaper than `v_readlane_b32` because the hardware does not need to read the lane index from the wave context. On Turing/Ada the corresponding lowering is `SHFL.IDX` with the lane-index source folded to the wave's lowest set bit, which the compiler typically handles with a dedicated `S2R SR_LANEMASK` plus `FNS` (find-N-set) sequence — but the call shape lets the back-end recognise the broadcast pattern and elide the lane-mask computation entirely when it can prove the wave is fully active. On Xe-HPG the same shape lights up the EU thread-broadcast fast path that the indexed variant does not.

The catch on `WaveReadLaneAt(x, 0)` is that lane 0 may not be *active*, while `WaveReadLaneFirst(x)` is defined as broadcasting from the first *active* lane — which on a partially-divergent wave entering the call from inside a branch is not lane 0. The two intrinsics have *different semantics* when the wave is partially active: `WaveReadLaneAt(x, 0)` is undefined if lane 0 is not in the active mask; `WaveReadLaneFirst(x)` is always defined as long as at least one lane is active. In practice every program that writes `WaveReadLaneAt(x, 0)` and reaches it from non-divergent control flow wants the broadcast-from-first-active behaviour — the literal `0` is a thinko for "the start of the wave", and the correct primitive is `WaveReadLaneFirst`. The rewrite is therefore both a performance fix (the lane-index plumbing goes away) *and* a correctness fix (the partially-active-wave case becomes well-defined). This is the rare case where machine-applying the rewrite is strictly safer than leaving the original, because the original has a latent UB that the rewrite removes.

## Examples

### Bad

```hlsl
// Lane 0 is the wrong primitive: the hardware emits the general lane-index
// broadcast and the call is UB if lane 0 is masked off.
[numthreads(64, 1, 1)]
void cs_publish_first(uint tid : SV_GroupIndex) {
    float partial = compute(tid);
    float wave_leader = WaveReadLaneAt(partial, 0);
    Output[GroupId.x] = wave_leader;
}
```

### Good

```hlsl
// WaveReadLaneFirst lights up the no-index broadcast fast path and is
// well-defined for any non-empty active mask.
[numthreads(64, 1, 1)]
void cs_publish_first(uint tid : SV_GroupIndex) {
    float partial = compute(tid);
    float wave_leader = WaveReadLaneFirst(partial);
    Output[GroupId.x] = wave_leader;
}
```

## Options

none

## Fix availability

**machine-applicable** — The fix replaces `WaveReadLaneAt(expr, 0)` with `WaveReadLaneFirst(expr)`, dropping the lane-index argument. The rewrite is strictly safer than the original: where the original is UB on a partially-active wave (lane 0 not in the active mask), the rewrite is well-defined as "the first active lane", which is the behaviour every well-written program written as `WaveReadLaneAt(x, 0)` actually wants. Codegen is also strictly cheaper on every IHV (RDNA `v_readfirstlane_b32` vs `v_readlane_b32`; Turing/Ada folded-source `SHFL.IDX`; Xe-HPG thread-broadcast fast path). `hlsl-clippy fix` applies the rewrite automatically.

## See also

- Related rule: [wave-intrinsic-non-uniform](wave-intrinsic-non-uniform.md) — flags wave intrinsics whose operand is non-uniform across the wave
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — surfaces the helper-lane interaction with wave intrinsics in pixel shaders
- Related rule: [wave-active-all-equal-precheck](wave-active-all-equal-precheck.md) — companion rule for redundant `WaveActiveAllEqual` calls before a broadcast
- HLSL intrinsic reference: `WaveReadLaneFirst`, `WaveReadLaneAt`, `WaveGetLaneIndex` in the DirectX HLSL Wave Intrinsics documentation
- Companion blog post: [math overview](../blog/math-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wavereadlaneat-constant-zero-to-readfirst.md)

*© 2026 NelCit, CC-BY-4.0.*
