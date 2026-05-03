---
id: fromrayquery-invoke-without-shader-table
category: ser
severity: error
applicability: none
since-version: v0.5.0
phase: 4
language_applicability: ["hlsl", "slang"]
---

# fromrayquery-invoke-without-shader-table

> **Status:** shipped (Phase 4) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

A `dx::HitObject::FromRayQuery(...)` value passed to `Invoke(...)` without an intervening `SetShaderTableIndex(...)` call on the same HitObject on every CFG path between construction and invocation. The SER spec requires `FromRayQuery`-constructed HitObjects to carry an explicit shader-table index before they can be invoked — the inline `RayQuery` does not record one because it has no concept of a shader table at traversal time. The Phase 4 definite-assignment analysis walks the CFG between construction and invocation.

## Why it matters on a GPU

`HitObject::FromRayQuery` constructs a HitObject from the result of an inline `RayQuery::TraceRayInline + Proceed` traversal. The inline traversal does not consult a shader binding table — that is the whole point of the inline path — so the resulting HitObject has no shader-table index field set. When the application then wants to invoke a hit / miss shader through the SER path, it needs to tell the runtime which shader to dispatch; that's `SetShaderTableIndex`.

Calling `Invoke` on a `FromRayQuery`-constructed HitObject without setting the shader-table index is undefined behaviour. On NVIDIA Ada Lovelace, the runtime's behaviour depends on the driver: some versions silently dispatch shader 0; some fault. AMD RDNA 4's SER implementation behaves similarly, with vendor-dependent recovery. There is no clean error message at runtime; the lint catches the violation by walking the CFG between the two calls and verifying that every path has an intervening `SetShaderTableIndex`.

The fix is to insert the missing call. The diagnostic names the offending CFG path and points at the spec.

## Examples

### Bad

```hlsl
// FromRayQuery -> Invoke without SetShaderTableIndex — UB.
[shader("raygeneration")]
void RayGen() {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeRay());
    while (q.Proceed()) {}
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        dx::HitObject hit = dx::HitObject::FromRayQuery(q);
        // missing hit.SetShaderTableIndex(...)
        hit.Invoke(/*payload*/);                          // ERROR: no STI
    }
}
```

### Good

```hlsl
[shader("raygeneration")]
void RayGen() {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeRay());
    while (q.Proceed()) {}
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        dx::HitObject hit = dx::HitObject::FromRayQuery(q);
        hit.SetShaderTableIndex(GetHitGroupIndex(q));     // required
        hit.Invoke(/*payload*/);
    }
}
```

## Options

none

## Fix availability

**none** — The right shader-table index is application-specific; the diagnostic names the missing call.

## See also

- Related rule: [hitobject-stored-in-memory](hitobject-stored-in-memory.md) — companion SER programming-model rule
- Related rule: [hitobject-construct-outside-allowed-stages](hitobject-construct-outside-allowed-stages.md) — companion rule
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/fromrayquery-invoke-without-shader-table.md)

*© 2026 NelCit, CC-BY-4.0.*
