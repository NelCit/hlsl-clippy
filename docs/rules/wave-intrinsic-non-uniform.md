---
id: wave-intrinsic-non-uniform
category: control-flow
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# wave-intrinsic-non-uniform

> **Status:** pre-v0 — rule scheduled for Phase 4; see [ROADMAP](../../ROADMAP.md).

## What it detects

Calls to any wave intrinsic that operates across the active lane set — `WaveActiveSum`, `WaveActiveProduct`, `WaveActiveMin`, `WaveActiveMax`, `WaveActiveBitAnd`, `WaveActiveBitOr`, `WaveActiveBitXor`, `WaveActiveAllTrue`, `WaveActiveAnyTrue`, `WaveActiveAllEqual`, `WaveActiveBallot`, `WavePrefixSum`, `WavePrefixProduct`, `WaveReadLaneFirst`, `WaveReadLaneAt`, `WaveMatch`, `WaveMultiPrefixSum`, and `WaveMultiPrefixProduct` — when they appear inside a branch whose condition is non-uniform across the threads of the wave. The rule fires when the predicate is derived from per-thread varying data (thread IDs, buffer reads with thread-varying index, per-pixel varying inputs) rather than from a provably uniform source (cbuffer fields, literal constants, `WaveIsFirstLane` results).

## Why it matters on a GPU

Wave intrinsics operate across the set of lanes that are currently active at the instruction's execution point. When all lanes in a wave enter the intrinsic together, the operation is well-defined: `WaveActiveSum(x)` sums the value `x` from every lane in the wave. But when the intrinsic executes inside a divergent branch, only the subset of lanes that took that branch are active — the rest are masked off by the hardware. The result of `WaveActiveSum` in this position is the sum across only the participating subset, not the full wave. This is almost never what the programmer intended; more importantly, the participating subset varies from wave to wave depending on data values, making the result non-deterministic across runs on the same input if the hardware scheduler changes wave composition. The D3D12 and Vulkan specifications classify this as undefined behaviour for operations that require wave uniformity at entry.

On AMD RDNA and RDNA 2/3 architectures, the wave size is 32 or 64 (configurable via `[WaveSize(...)]`). The cross-lane operations are implemented as VALU horizontal reduction instructions (e.g., `ds_swizzle`, `v_permlanex16`, `v_permlane64`). When only a subset of lanes is active, the hardware reduction still executes across the physical wave but masks the inactive lanes at writeback. The result is the reduction of the active subset, which for accumulative operations like `WaveActiveSum` produces a value smaller than the full-wave sum. Code that uses this value to allocate output slots (e.g., `WavePrefixSum(1u)` for compact output offset) will produce incorrect offsets and potentially out-of-bounds writes. On NVIDIA Turing and Ada, the behaviour is analogous: SHFL-based reductions operate on the active warp mask, and the returned value in a divergent context is the reduction of the active subset only.

Beyond the incorrect result, placing wave intrinsics inside divergent branches that are not reconverged before the intrinsic is a correctness hazard that interacts poorly with driver-level optimisations. Some drivers speculatively promote wave intrinsics to sub-wave operations for throughput, which can change the active mask semantics compared to a non-optimised execution. For audit trail purity, the D3D12 HLSL specification requires that the control flow leading to any wave intrinsic must be uniform (all lanes take the same path), or the intrinsic must be one of the explicitly lane-query variants (`WavePrefixSum` when used with `WaveBallot` guards, etc.). The safest approach is to move wave intrinsics to the start of the shader, use explicit ballot-based guards, or restructure the algorithm to apply the wave reduction before any data-driven branch.

## Examples

### Bad

```hlsl
// From tests/fixtures/phase4/control_flow.hlsl, line 34-42
// HIT(wave-intrinsic-non-uniform): WaveActiveSum requires uniform
// entry; participating lanes differ here.
[numthreads(64, 1, 1)]
void cs_wave_op_divergent(uint3 dtid : SV_DispatchThreadID) {
    if ((dtid.x & 1u) != 0u) {
        // Only odd-indexed threads reach this WaveActiveSum.
        // The result is the sum of odd-lane values only, not the full wave.
        uint s = WaveActiveSum(dtid.x);
        Counter[0] = s;
    }
}

// From tests/fixtures/phase4/control_flow_extra.hlsl, line 84-93
// HIT(wave-intrinsic-non-uniform): WavePrefixSum in a data-dependent
// branch — participating lane set is non-uniform.
[numthreads(64, 1, 1)]
void cs_wave_divergent_ballot(uint3 dtid : SV_DispatchThreadID) {
    float noise = NoiseTex.Load(int3(dtid.xy, 0)).r;
    if (noise > Threshold) {
        // 'noise' is per-thread data. WavePrefixSum here sees only the
        // subset of lanes whose noise value exceeded the threshold.
        uint prefix = WavePrefixSum(dtid.x);
        HitCounts[prefix & 0x3Fu] = dtid.x;
    }
}
```

### Good

```hlsl
// Use WaveActiveBallot to count qualifying lanes, then branch on WaveIsFirstLane
// for a single atomic write — wave intrinsic in uniform CF.
[numthreads(64, 1, 1)]
void cs_wave_divergent_fixed(uint3 dtid : SV_DispatchThreadID) {
    // Wave intrinsic executed in uniform control flow — all lanes participate.
    uint4 ballot = WaveActiveBallot((dtid.x & 1u) != 0u);
    uint oddCount = countbits(ballot.x) + countbits(ballot.y)
                  + countbits(ballot.z) + countbits(ballot.w);
    if (WaveIsFirstLane()) {
        // One lane does the write on behalf of the wave.
        InterlockedAdd(Counter[0], oddCount);
    }
}

// For the compact-output pattern: compute prefix in uniform CF before branching.
[numthreads(64, 1, 1)]
void cs_wave_prefix_fixed(uint3 dtid : SV_DispatchThreadID) {
    float noise = NoiseTex.Load(int3(dtid.xy, 0)).r;
    bool  active = noise > Threshold;
    // WavePrefixSum in uniform control flow — all lanes participate correctly.
    uint  prefix = WavePrefixCountBits(active);
    if (active) {
        HitCounts[prefix & 0x3Fu] = dtid.x;
    }
}
```

## Options

none

## Fix availability

**none** — Moving a wave intrinsic out of divergent control flow changes the set of lanes that contribute to the result. Determining whether the move is semantically equivalent requires understanding the algorithm's intent — it is not a pure textual substitution. The diagnostic identifies the intrinsic call and the non-uniform predicate expression.

## See also

- Related rule: [barrier-in-divergent-cf](barrier-in-divergent-cf.md) — barriers inside divergent CF (deadlock hazard)
- Related rule: [derivative-in-divergent-cf](derivative-in-divergent-cf.md) — derivatives in divergent CF (UB in pixel shaders)
- Related rule: [wave-intrinsic-helper-lane-hazard](wave-intrinsic-helper-lane-hazard.md) — helper-lane participation in wave ops after `discard`
- Related rule: [wave-active-all-equal-precheck](wave-active-all-equal-precheck.md) — using `WaveActiveAllEqual` to select the uniform fast path
- HLSL intrinsic reference: `WaveActiveSum`, `WavePrefixSum`, `WaveActiveBallot` in the DirectX HLSL Intrinsics documentation
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/wave-intrinsic-non-uniform.md)

<!-- © 2026 NelCit, CC-BY-4.0. Code snippets are Apache-2.0. -->
