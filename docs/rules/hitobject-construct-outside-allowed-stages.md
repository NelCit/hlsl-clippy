---
id: hitobject-construct-outside-allowed-stages
category: ser
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# hitobject-construct-outside-allowed-stages

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A `dx::HitObject::TraceRay`, `dx::HitObject::FromRayQuery`, or `dx::HitObject::MakeMiss` constructor call from a stage other than the SM 6.9 SER spec's allowed set: raygeneration, closest-hit, and miss (with stage-specific restrictions per the spec's table). The rule reads the entry-point stage from Slang reflection and matches each constructor against the spec's allowed-stages set, firing when a call originates from any other stage (any-hit, intersection, callable, compute, mesh, amplification, vertex, pixel, etc.).

## Why it matters on a GPU

The SER programming model assumes that `dx::HitObject` is constructed at well-defined hardware points where the RT subsystem can hand a coherent traversal record back to the shader. On NVIDIA Ada Lovelace, those points correspond to the SM-side handoff slots that the RT cores expose; the hardware does not present the same handoff in any-hit (where the traversal is suspended mid-leaf, not committed) or intersection (where the procedural primitive's existence is still being evaluated). On future AMD and Intel SER implementations, the same hardware-scoped restriction applies.

Constructing a HitObject from a disallowed stage is undefined behaviour. The DXC validator catches the most common forms and emits an error; runtime drivers do not attempt to handle the construction. As with the sibling SER rules, surfacing this at lint time replaces a hard PSO-link failure with a precise source-located diagnostic.

The fix is structural: factor the work that produces the HitObject into raygen (using `TraceRay` / `FromRayQuery`), pass any per-hit context through the payload, and let the closest-hit / miss handlers consume the payload normally. SER's whole point is that the reorder happens *between* traversal and shader-invocation, both of which are raygen-scoped operations.

## Examples

### Bad

```hlsl
// Any-hit cannot construct a HitObject — the traversal is mid-leaf.
[shader("anyhit")]
void OnAnyHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attr) {
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, ray, payload); // ERROR
    hit.Invoke(payload);
}
```

### Good

```hlsl
// HitObject construction lives in raygen.
[shader("raygeneration")]
void RayGen() {
    RayDesc ray = MakePrimaryRay();
    RayPayload payload = (RayPayload)0;
    dx::HitObject hit = dx::HitObject::TraceRay(g_BVH, RAY_FLAG_NONE, 0xFF,
                                                0, 1, 0, ray, payload);
    dx::MaybeReorderThread(hit);
    hit.Invoke(payload);
}
```

## Options

none

## Fix availability

**none** — Moving the construction is a structural refactor.

## See also

- Related rule: [hitobject-stored-in-memory](hitobject-stored-in-memory.md) — sibling SER programming-model rule
- Related rule: [maybereorderthread-outside-raygen](maybereorderthread-outside-raygen.md) — sibling SER programming-model rule
- Related rule: [hitobject-passed-to-non-inlined-fn](hitobject-passed-to-non-inlined-fn.md) — inter-procedural sibling rule
- HLSL specification: [proposal 0027 Shader Execution Reordering](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- Companion blog post: [ser overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/hitobject-construct-outside-allowed-stages.md)

*© 2026 NelCit, CC-BY-4.0.*
