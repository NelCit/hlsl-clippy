---
id: hitobject-passed-to-non-inlined-fn
category: ser
severity: error
applicability: none
since-version: v0.5.0
phase: 4
---

# hitobject-passed-to-non-inlined-fn

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `dx::HitObject` value passed as an argument to or returned from a function that the call-graph analysis cannot prove is inlined into its raygen caller. The SM 6.9 SER specification requires HitObject lifetimes to stay inside an inlined call chain; a non-inlined function boundary forces the runtime to spill the HitObject across the call, which is undefined behaviour. The Phase 4 inter-procedural analysis tracks `[noinline]` annotations, recursive calls, and indirect calls (`function pointers`) that block inlining.

## Why it matters on a GPU

The SER programming model bakes inlining into its hardware contract: the HitObject's per-lane state lives in registers that the RT cores own jointly with the SM, and the runtime only knows how to materialise / dematerialise that state at well-defined boundaries (raygen entry, `Invoke`, `MaybeReorderThread`). When a HitObject crosses a non-inlined call boundary, the calling convention has no recipe for spilling it — the spec marks the operation undefined precisely because no IHV has a defined hardware path for the spill. NVIDIA Ada Lovelace's compiler emits a hard error for the simplest forms; future implementations may either emit garbage or fault.

The DXC validator catches the trivial cases (a `HitObject` parameter on a function annotated `[noinline]`); the Phase 4 call-graph analysis catches the rest, including the case where a function is implicitly non-inlined because it's recursive, or because it's called through a function pointer the linker can't resolve. The diagnostic names the offending function and the call site so the author can either inline the function (drop `[noinline]`, refactor away from recursion / indirection) or restructure the work so the HitObject doesn't cross the boundary.

## Examples

### Bad

```hlsl
// Function is recursive — implicitly non-inlinable.
dx::HitObject TraceRecurse(uint depth, RayPayload p) {
    if (depth == 0) return dx::HitObject::MakeMiss(0, MakeRay(), 0);
    return TraceRecurse(depth - 1, p);  // recursion blocks inlining
}

[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = TraceRecurse(3, p);  // ERROR: HitObject across non-inlined call
    hit.Invoke(p);
}
```

### Good

```hlsl
// Iterate inline within raygen; HitObject stays in registers.
[shader("raygeneration")]
void RayGen() {
    RayPayload p = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, MakeRay(), p);
    [unroll] for (uint i = 0; i < 3; ++i) {
        if (HitDescribesMiss(hit)) break;
        // refine the HitObject in-place — all in registers.
    }
    hit.Invoke(p);
}
```

## Options

none

## Fix availability

**none** — Either inline the offending function or restructure the work; both are authorial.

## See also

- Related rule: [hitobject-stored-in-memory](hitobject-stored-in-memory.md) — sibling SER programming-model rule
- Related rule: [hitobject-construct-outside-allowed-stages](hitobject-construct-outside-allowed-stages.md) — sibling rule
- Related rule: [hitobject-invoke-after-recursion-cap](hitobject-invoke-after-recursion-cap.md) — recursion cap on the Invoke side
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/hitobject-passed-to-non-inlined-fn.md)

*© 2026 NelCit, CC-BY-4.0.*
