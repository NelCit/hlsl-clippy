---
id: groupshared-volatile
category: workgroup
severity: warn
applicability: machine-applicable
since-version: v0.2.0
phase: 2
---

# groupshared-volatile

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

Declarations of the form `volatile groupshared T name[...];` (or any other ordering of the `volatile` and `groupshared` storage qualifiers on the same declaration). The match is purely structural — the rule fires whenever the `volatile` keyword appears in the qualifier list of a declaration that also carries `groupshared`. It does not look at how the variable is subsequently used; the `volatile` is meaningless on `groupshared` storage regardless of access pattern. The rule does not fire on plain `volatile` locals (no `groupshared`) because those occasionally have legitimate uses around inline assembly–style spinning, nor on `globallycoherent` UAV declarations, which are the *real* mechanism HLSL provides for cross-thread visibility.

## Why it matters on a GPU

HLSL's memory model (DXC and Slang both follow the DXIL spec on this) treats `volatile` as a hint to suppress *intra-thread* reordering of loads and stores — the same per-thread guarantee C inherits from its origin as a systems language. It says nothing about cross-thread visibility. On `groupshared` storage, where the entire reason a variable exists is to communicate between threads in a workgroup, this guarantee is exactly the wrong one. Programmers reach for `volatile` when they want "every thread sees the latest value"; they get the per-thread reordering fence and *no* cross-thread synchronisation. The actual mechanisms HLSL provides for that are `GroupMemoryBarrierWithGroupSync()` (or `GroupMemoryBarrier()` for the no-execution-sync variant) and, for UAV traffic, the `globallycoherent` storage class.

The hardware consequences are concrete on every IHV. AMD RDNA 2/3 schedules LDS accesses through a per-CU 32-bank crossbar; the compiler aggressively coalesces and reorders LDS loads/stores within a wave to maximise bank parallelism, and it relies on the optimiser's freedom to do so. Marking the LDS variable `volatile` defeats those reorderings and forces each access to issue as written, in source order, with no coalescing — the LDS bandwidth budget per wave drops measurably while delivering exactly zero cross-thread visibility. NVIDIA Turing and Ada Lovelace map `groupshared` onto the configurable L1/shared-memory partition with similar bank-parallel scheduling; here too the optimiser depends on being free to reorder shared-memory traffic, and `volatile` in DXIL lowers to a `noalias`-stripping attribute on the LDS pointer that pessimises the entire access chain. Intel Xe-HPG's SLM scheduler is no different in this respect.

The clinching point is that the rewrite is free: dropping `volatile` cannot change observable behaviour, because the `volatile` semantic the programmer wanted (cross-thread visibility) was never delivered in the first place. If the surrounding code relies on a cross-thread invariant, it was already broken — the fix is to add a `GroupMemoryBarrierWithGroupSync()` at the right point, and the linter surfaces that need by removing the qualifier that was masquerading as a synchronisation primitive. Either the original code was correct without `volatile` (the common case — the qualifier was cargo-culted in from a CPU-side mental model) or the original code was racy (in which case the missing barrier becomes visible).

## Examples

### Bad

```hlsl
// volatile gives per-thread fencing, not the cross-thread visibility the author wanted.
volatile groupshared float scratch[256];

[numthreads(256, 1, 1)]
void cs_partial_sum(uint tid : SV_GroupIndex) {
    scratch[tid] = compute_partial(tid);
    // BUG: no barrier; volatile does NOT make scratch[tid - 1] visible across threads.
    if (tid > 0) {
        scratch[tid] += scratch[tid - 1];
    }
}
```

### Good

```hlsl
// Drop the volatile qualifier; add the barrier the original code actually needed.
groupshared float scratch[256];

[numthreads(256, 1, 1)]
void cs_partial_sum(uint tid : SV_GroupIndex) {
    scratch[tid] = compute_partial(tid);
    GroupMemoryBarrierWithGroupSync();
    if (tid > 0) {
        scratch[tid] += scratch[tid - 1];
    }
}
```

## Options

none

## Fix availability

**machine-applicable** — The fix removes the `volatile` token from the declaration's qualifier list and leaves everything else untouched. Because `volatile` on `groupshared` storage delivers no cross-thread guarantee in HLSL's memory model, the removal cannot change observable behaviour: any code that relied on cross-thread visibility was already racy and needs an explicit `GroupMemoryBarrier*` call. `hlsl-clippy fix` applies the rewrite automatically and emits a one-line note pointing at `GroupMemoryBarrierWithGroupSync` for cases where the developer intended cross-thread synchronisation.

## See also

- Related rule: [groupshared-uninitialized-read](groupshared-uninitialized-read.md) — surfaces the missing-barrier hazard when a thread reads a cell no thread has yet written
- Related rule: [groupshared-write-then-no-barrier-read](groupshared-write-then-no-barrier-read.md) — partner rule for the missing-barrier-after-write case
- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — flags the inverse footgun (a barrier that may not be reached by all lanes)
- HLSL reference: `groupshared` storage class, `GroupMemoryBarrier`, `GroupMemoryBarrierWithGroupSync`, `globallycoherent` in the DirectX HLSL language reference
- Companion blog post: [workgroup overview](../blog/workgroup-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-volatile.md)

*© 2026 NelCit, CC-BY-4.0.*
