---
id: ray-flag-force-opaque-with-anyhit
category: dxr
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# ray-flag-force-opaque-with-anyhit

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `TraceRay(...)` call with `RAY_FLAG_FORCE_OPAQUE` set in a translation unit
that also defines a `[shader("anyhit")]` entry point.

## Why it matters on a GPU

`RAY_FLAG_FORCE_OPAQUE` skips AnyHit invocation. Binding an AnyHit and then
forcing opaque is dead code or a logic bug -- the AnyHit shader will never
run. On RDNA 4, NVIDIA Ada/Blackwell and Intel Xe2, the AnyHit binding still
costs scheduling table slots even when never invoked.

## Examples

### Bad

```hlsl
[shader("anyhit")] void ah(inout Payload p, in Attribs a) {}
[shader("raygeneration")] void rg() {
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 1, 0, r, p); // ah never fires
}
```

## Options

none

## Fix availability

**suggestion** — Either drop the AnyHit shader from the binding or remove
`RAY_FLAG_FORCE_OPAQUE` from the call.
