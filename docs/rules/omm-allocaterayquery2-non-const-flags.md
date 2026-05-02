---
id: omm-allocaterayquery2-non-const-flags
category: opacity-micromaps
severity: error
applicability: none
since-version: v0.3.0
phase: 3
---

# omm-allocaterayquery2-non-const-flags

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0010)*

## What it detects

An `AllocateRayQuery2(...)` call (the DXR 1.2 form that takes both static and dynamic ray flags) whose first argument — the *constant* ray-flag bundle — is not a compile-time constant. Constant-folding through the AST identifies whether the argument resolves to a literal; the rule fires when it does not.

## Why it matters on a GPU

`AllocateRayQuery2(constFlags, dynFlags)` is the DXR 1.2 split-flag intrinsic that lets the developer pass some ray flags as compile-time constants (which the compiler can pattern-match into specialised RayQuery template instantiations) and others as runtime values (which stay generic). The split exists so the compiler can specialise expensive ray-flag combinations — `RAY_FLAG_FORCE_OPAQUE`, `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`, the OMM flags — at compile time, while leaving cheap conditional flags (`RAY_FLAG_SKIP_TRIANGLES` based on a per-thread mask) for runtime.

The first argument *must* be a compile-time constant: the entire point of the split is that the compiler templates the RayQuery on its bits. DXC enforces this hard: a non-constant first argument is a compile error. The lint catches the more interesting cases that DXC may struggle with — for instance, an argument that comes from a `static const` global whose value is computed by an inline function, or an argument that is folded behind a `#define` whose macro expansion the AST shows but DXC's frontend treats opaquely.

The fix is to make the argument literal: either inline the value or move the runtime portion to the second `dynFlags` argument. The diagnostic names the call site and points at the spec.

## Examples

### Bad

```hlsl
// First argument is computed at runtime — error.
RaytracingAccelerationStructure g_BVH : register(t0);
cbuffer Cfg : register(b0) { uint g_RuntimeFlags; }

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    RayQuery q = AllocateRayQuery2(g_RuntimeFlags,                 // ERROR
                                    RAY_FLAG_NONE);
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeRay(tid));
}
```

### Good

```hlsl
// Constant first argument; runtime bits go to the second argument.
RaytracingAccelerationStructure g_BVH : register(t0);
cbuffer Cfg : register(b0) { uint g_RuntimeFlags; }

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    RayQuery q = AllocateRayQuery2(
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
        g_RuntimeFlags);
    q.TraceRayInline(g_BVH, 0, 0xFF, MakeRay(tid));
}
```

## Options

none

## Fix availability

**none** — Splitting the argument requires authorial intent (which bits are static vs. dynamic).

## See also

- Related rule: [omm-rayquery-force-2state-without-allow-flag](omm-rayquery-force-2state-without-allow-flag.md) — companion OMM rule
- Related rule: [omm-traceray-force-omm-2state-without-pipeline-flag](omm-traceray-force-omm-2state-without-pipeline-flag.md) — companion OMM rule
- HLSL specification: [proposal 0024 Opacity Micromaps](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0024-opacity-micromaps.md)
- Companion blog post: [opacity-micromaps overview](../blog/ser-coop-vector-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/omm-allocaterayquery2-non-const-flags.md)

*© 2026 NelCit, CC-BY-4.0.*
