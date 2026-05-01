---
id: dispatchmesh-not-called
category: mesh
severity: error
applicability: none
since-version: v0.4.0
phase: 4
---

# dispatchmesh-not-called

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0011)*

## What it detects

An amplification shader entry point (`[shader("amplification")]` or any function with the amplification stage tag in reflection) where at least one CFG path from entry to function return does not call `DispatchMesh(x, y, z, payload)`. The rule fires on early returns guarded by per-thread conditions, on switch arms missing the dispatch call, and on loops whose exit path bypasses the dispatch. CFG analysis treats `DispatchMesh` as required-to-execute exactly once per amplification thread group.

## Why it matters on a GPU

The amplification stage in the D3D12 mesh pipeline is contractually required to call `DispatchMesh` exactly once per launched thread group. The call hands control (and an optional payload) from the amplification phase to the mesh-shader phase. If the call is missed on any thread group's execution path, the hardware behaviour is undefined: AMD RDNA 2/3 may deadlock the geometry front-end waiting for the launch, NVIDIA Ada may silently drop the meshlet (the visual symptom is missing geometry on a fraction of frames), and Intel Xe-HPG may surface a TDR. The reproducibility is platform- and driver-dependent, which makes the bug a classic "works on the dev's machine, breaks in QA" hazard.

The most common origin of the bug is an early-return guard added during refactoring: "if my amplification thread group has no surviving meshlets, return early to skip the dispatch". The intent is performance — skip the empty `DispatchMesh(0, 1, 1)` — but the actual behaviour is undefined because the contract requires the call. The correct phrasing is to call `DispatchMesh(0, 1, 1)` (or whatever the runtime treats as a no-op-equivalent dispatch) and let the runtime handle the empty case. A second common origin is `switch` cases that diverge on per-launch state; if any case omits the call, the contract breaks for that case. The rule's CFG analysis enumerates every return-reaching path and reports the first one missing the call.

The fix is uniform: ensure exactly one `DispatchMesh` call dominates every return path. The HLSL specification does not allow more than one `DispatchMesh` per thread group either, so the analysis also surfaces the rare case of two calls on different paths (a sibling rule, not in this scope, will catch the duplicate-call case). The diagnostic distinguishes "no call on any path" from "call missing on some path" so the author can audit accordingly.

## Examples

### Bad

```hlsl
[shader("amplification")]
[numthreads(32, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID) {
    Payload p = build_payload(gtid);

    // Early return when no meshlets survive — but the contract requires
    // DispatchMesh to be called exactly once. UB on RDNA / Ada / Xe-HPG.
    if (p.meshletCount == 0) {
        return;
    }

    DispatchMesh(p.meshletCount, 1, 1, p);
}
```

### Good

```hlsl
[shader("amplification")]
[numthreads(32, 1, 1)]
void as_main_safe(uint gtid : SV_GroupThreadID) {
    Payload p = build_payload(gtid);

    // Always call DispatchMesh. The runtime handles the (0, 1, 1) case as
    // a no-op launch — much cheaper than UB.
    DispatchMesh(max(p.meshletCount, 0), 1, 1, p);
}

// For switch / branch cases, ensure every reachable return calls DispatchMesh:
[shader("amplification")]
[numthreads(32, 1, 1)]
void as_dispatch_per_mode(uint gtid : SV_GroupThreadID) {
    Payload p = build_payload(gtid);
    switch (p.mode) {
        case MODE_A:
            DispatchMesh(p.countA, 1, 1, p);
            break;
        case MODE_B:
            DispatchMesh(p.countB, 1, 1, p);
            break;
        default:
            // Was missing the call — fixed.
            DispatchMesh(0, 1, 1, p);
            break;
    }
}
```

## Options

none

## Fix availability

**none** — Inserting a `DispatchMesh` call requires choosing the correct grid dimensions and payload value for the missing path; both are algorithm-specific. The diagnostic identifies the missing-call return path and the function entry so the author can supply the right call.

## See also

- Related rule: [setmeshoutputcounts-in-divergent-cf](setmeshoutputcounts-in-divergent-cf.md) — analogous output-completeness rule on the mesh side
- Related rule: [outputcomplete-missing](outputcomplete-missing.md) — mesh-output completeness
- Related rule: [primcount-overrun-in-conditional-cf](primcount-overrun-in-conditional-cf.md) — companion mesh-output-count rule
- HLSL reference: amplification-shader specification, `DispatchMesh` semantics
- Companion blog post: _not yet published — will appear alongside the v0.4.0 release_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/dispatchmesh-not-called.md)

*© 2026 NelCit, CC-BY-4.0.*
