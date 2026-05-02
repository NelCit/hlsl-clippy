---
id: recursion-depth-not-declared
category: dxr
severity: warn
applicability: suggestion
since-version: v0.7.0
phase: 7
---

# recursion-depth-not-declared

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

## What it detects

DXR pipeline-state-object construction sites (in companion C++ source consumed by the linter, or in `[shader("raygeneration")]` entry-point metadata when authored in pure HLSL builds) that fail to set `MaxTraceRecursionDepth` on the `D3D12_RAYTRACING_PIPELINE_CONFIG` subobject. The rule additionally fires when the PSO sets a recursion depth that does not match the static call-graph depth of `TraceRay` calls observable in the linked shader set: too small a depth means runtime errors when a deep chain runs; too large a depth means the driver pre-allocates a larger ray stack than the shader will ever use.

## Why it matters on a GPU

`MaxTraceRecursionDepth` is not a hint — it is a hard sizing parameter the driver uses to allocate the per-lane ray stack at PSO creation time. The runtime multiplies the declared depth by the maximum payload + attribute + per-call live-state footprint to compute the stack size, then allocates that amount of scratch memory per lane in the launch grid. On a 1080p `DispatchRays` with one ray per pixel and 64-lane waves on RDNA 3, the ray-stack pool sized at `MaxTraceRecursionDepth = 8` versus `MaxTraceRecursionDepth = 2` is a four-times difference in allocated scratch — easily tens of megabytes of VRAM that sits unused if the actual depth is shallower.

Worse, unused depth is not free at runtime either. The driver's ray-stack allocator on AMD RDNA 2/3 and NVIDIA Ada Lovelace places the stack in a high-address region of GPU memory; oversized stacks push out of L2 cache more aggressively, evicting BVH and texture data that would otherwise be resident. Intel Xe-HPG measurements have shown 5-12% performance regressions in path-traced workloads when the declared recursion depth is set conservatively high (e.g., 16) compared to the matched value (4 for typical reflection + shadow). The microsoft validation layer warns about exceeding the declared depth at runtime, but it does not warn about over-declaring, which is exactly the case this rule covers.

When the value is omitted entirely, the runtime defaults vary by driver but commonly fall back to 1, which means a shader that performs even a single secondary trace will fail at dispatch with an undecorated DXGI device-removed error. The pairing of these two failure modes — silent over-allocation and silent runtime crash — is why this rule errs on the side of suggesting an explicit, call-graph-derived value computed by walking `TraceRay` call edges in the IR.

## Examples

### Bad

```cpp
// C++ side — PSO subobject construction with no MaxTraceRecursionDepth.
D3D12_RAYTRACING_PIPELINE_CONFIG cfg = {};
// cfg.MaxTraceRecursionDepth = 0;  // unset — driver default applies
psoSubobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &cfg });
```

```hlsl
// HLSL side — closest-hit recurses for reflections; static depth is 4.
[shader("closesthit")]
void ReflectHit(inout Payload p, BuiltInTriangleIntersectionAttributes a) {
    if (p.bounce < 3) {
        p.bounce++;
        TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 1, 0, MakeReflectRay(), p);
    }
}
```

### Good

```cpp
// Match the static depth observed in the shader call graph: primary + 3 bounces = 4.
D3D12_RAYTRACING_PIPELINE_CONFIG cfg = {};
cfg.MaxTraceRecursionDepth = 4;
psoSubobjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &cfg });
```

## Options

- `slack` (integer, default: `0`) — additional depth permitted above the statically observed maximum, to accommodate runtime data-driven recursion that the static analyser cannot see. Set to `1` or `2` for shaders whose recursion depth is bounded by uniform input data the rule cannot read.
- `cap` (integer, default: `8`) — absolute upper bound on suggested values; if the static analysis reports more, the rule emits a separate diagnostic suggesting the recursion be flattened into iteration.

## Fix availability

**suggestion** — The rule reports the statically observed maximum recursion depth and suggests setting `MaxTraceRecursionDepth` to that value. Because runtime control flow may legitimately recurse deeper than the static analysis can prove, the user must confirm the suggested value before applying the fix.

## See also

- Related rule: [oversized-ray-payload](oversized-ray-payload.md) — payload size multiplies with declared recursion depth in stack allocation
- Related rule: [live-state-across-traceray](live-state-across-traceray.md) — ray-stack spills compound with recursion depth
- Related rule: [missing-accept-first-hit](missing-accept-first-hit.md) — shadow PSOs should pair `MaxTraceRecursionDepth = 1` with first-hit termination
- DXR specification: `D3D12_RAYTRACING_PIPELINE_CONFIG` subobject documentation
- Companion blog post: [dxr overview](../blog/mesh-dxr-overview.md)

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/recursion-depth-not-declared.md)

*© 2026 NelCit, CC-BY-4.0.*
