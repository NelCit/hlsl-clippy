---
id: groupshared-when-registers-suffice
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
---

# groupshared-when-registers-suffice

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

A `groupshared` array used as per-thread scratch — written by thread `tid` and read only by thread `tid` (no cross-thread access) — whose size per thread is small enough that the compiler could keep it in registers. Default threshold: 8 elements per thread. The Phase 7 IR-level register-pressure analysis verifies that promoting the array to private (per-thread) registers does not push the kernel over the per-CU/SM register budget.

## Why it matters on a GPU

LDS / shared-memory is a precious resource on every modern GPU. AMD RDNA 2/3 has 64 KB of LDS per CU shared across all in-flight workgroups — a kernel that allocates 8 KB of LDS per workgroup limits in-flight workgroups to 8 per CU, which directly caps the kernel's wave occupancy and its ability to hide memory latency. NVIDIA Turing/Ada Lovelace has 100 KB of shared memory per SM with similar trade-offs. Intel Xe-HPG has 128 KB of SLM per Xe core. The marginal occupancy cliff is steep — going from 8 KB to 4 KB per workgroup typically doubles in-flight workgroups, doubles latency hiding, and can deliver 1.5-3x perf on memory-bound kernels.

When a `groupshared` array is used purely as per-thread scratch (e.g., a small per-thread accumulator buffer), it doesn't actually need to be in LDS — it only needs to persist across the kernel's execution. Modern compilers can keep 4-8 element per-thread arrays in registers if the access pattern is statically analysable. The fix is to declare the array as a local variable inside the kernel rather than `groupshared`; the compiler will spill to scratch only if register pressure forces it.

The rule is research-grade Phase 7 because the register-pressure check requires IR-level reasoning: declaring a large per-thread array as local doesn't help if the compiler then spills it to scratch (which is worse than LDS for cross-thread access). The Phase 7 register-pressure infrastructure (shared with `vgpr-pressure-warning`) determines whether the promotion is profitable.

## Examples

### Bad

```hlsl
// Per-thread accumulator in groupshared — wastes LDS, caps occupancy.
groupshared float s_acc[64][8];   // 64 threads * 8 floats = 2 KB

[numthreads(64, 1, 1)]
void main(uint tid : SV_GroupThreadID) {
    [unroll] for (uint i = 0; i < 8; ++i) {
        s_acc[tid][i] = ComputeSample(tid, i);
    }
    float total = 0;
    [unroll] for (uint i = 0; i < 8; ++i) {
        total += s_acc[tid][i];
    }
    g_Output[tid] = total;
}
```

### Good

```hlsl
// Per-thread accumulator in registers — frees the LDS budget.
[numthreads(64, 1, 1)]
void main(uint tid : SV_GroupThreadID) {
    float acc[8];
    [unroll] for (uint i = 0; i < 8; ++i) {
        acc[i] = ComputeSample(tid, i);
    }
    float total = 0;
    [unroll] for (uint i = 0; i < 8; ++i) {
        total += acc[i];
    }
    g_Output[tid] = total;
}
```

## Options

- `max-elements-per-thread` (integer, default: `8`) — the per-thread array size threshold above which the rule does not fire (because register promotion is unlikely to be profitable).

## Fix availability

**suggestion** — Promoting the array requires changing the declaration scope and the index expressions; it is mechanical for simple cases but requires verification on more complex access patterns. The diagnostic emits the candidate rewrite as a comment.

## See also

- Related rule: [vgpr-pressure-warning](vgpr-pressure-warning.md) — companion register-pressure rule
- Related rule: [groupshared-too-large](groupshared-too-large.md) — companion LDS budget rule
- Related rule: [scratch-from-dynamic-indexing](scratch-from-dynamic-indexing.md) — when register promotion fails
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/groupshared-when-registers-suffice.md)

*© 2026 NelCit, CC-BY-4.0.*
