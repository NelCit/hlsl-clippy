---
id: ser-trace-then-invoke-without-reorder
category: ser
severity: warn
applicability: suggestion
since-version: v0.4.0
phase: 4
---

# ser-trace-then-invoke-without-reorder

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `dx::HitObject::TraceRay` (or `FromRayQuery`) construction whose only use is a direct `Invoke` call on the same HitObject, without an intervening `dx::MaybeReorderThread`. The Phase 4 reachability analysis walks from the construction site to the invocation site and verifies that no `MaybeReorderThread` exists on any path. This is the missed-opportunity counterpart to the SER programming-model rules.

## Why it matters on a GPU

The whole point of using `dx::HitObject::TraceRay` + `Invoke` instead of plain `TraceRay` is to give the runtime an opportunity to reorder lanes before the closest-hit / miss shader runs. If the application constructs a HitObject and invokes it immediately without calling `MaybeReorderThread`, the runtime gets no reorder opportunity — it dispatches the shaders in whatever order the lanes happen to land, exactly as plain `TraceRay` would. The HitObject machinery (which has its own per-lane register-spill cost on every IHV: NVIDIA Ada Lovelace, AMD RDNA 4 when shipped, Intel Xe-HPG when shipped) is paid for and discarded.

NVIDIA's SER perf blog explicitly calls out this pattern as a perf footgun — the HitObject path has a small fixed overhead vs. plain `TraceRay`, and that overhead pays off only when a reorder happens. Without the reorder, plain `TraceRay` is strictly cheaper.

The fix is one of: insert `MaybeReorderThread` between construction and invocation (if a meaningful coherence hint exists), or replace the HitObject path with plain `TraceRay`. The rule is suggestion-tier because in some workloads the HitObject path is a stepping stone (e.g., the `Invoke` is conditional on an earlier check that depends on `hit.IsHit()`); the diagnostic names the construction and invocation sites and asks the author to confirm.

## Examples

### Bad

```hlsl
// HitObject constructed and invoked with no reorder — pays HitObject overhead
// without recovering it through reorder.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    hit.Invoke(p);   // no MaybeReorderThread — plain TraceRay would be cheaper
}
```

### Good

```hlsl
// Either reorder before invoke ...
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    uint matId = LookupMaterialId(hit);
    dx::MaybeReorderThread(hit, matId, 6);
    hit.Invoke(p);
}

// ... or use plain TraceRay if no reorder is wanted.
```

## Options

none

## Fix availability

**suggestion** — The right replacement (insert reorder vs. drop the HitObject path) requires understanding the hit-shader workload's coherence opportunity. The diagnostic emits both candidate rewrites as comments.

## See also

- Related rule: [coherence-hint-redundant-bits](coherence-hint-redundant-bits.md) — companion SER perf rule
- Related rule: [coherence-hint-encodes-shader-type](coherence-hint-encodes-shader-type.md) — companion SER perf rule
- Related rule: [maybereorderthread-without-payload-shrink](maybereorderthread-without-payload-shrink.md) — companion SER perf rule
- Reference: [NVIDIA SER perf blog](https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering/)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/ser-trace-then-invoke-without-reorder.md)

*© 2026 NelCit, CC-BY-4.0.*
