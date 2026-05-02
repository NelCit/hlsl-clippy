---
id: dispatchmesh-grid-too-small-for-wave
category: mesh
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# dispatchmesh-grid-too-small-for-wave

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `DispatchMesh(x, y, z)` call where all three arguments are integer literals
and `x * y * z < expected_wave_size_for_target`.

## Why it matters on a GPU

Dispatching less than one wave wastes the entire dispatch: the lanes beyond
the grid still consume a full wave slot on every IHV. RDNA 2/3/4 wave32 has
32 lanes; Turing+ always 32. AMD's RDNA Performance Guide flags
sub-wave-sized dispatches as a measurable foot-gun for amplification
shaders (mesh-shading frontend) where workload sizing is often handcoded.

## Examples

### Bad

```hlsl
[numthreads(1, 1, 1)]
[shader("amplification")]
void as_main() { DispatchMesh(4, 1, 1); }
```

### Good

```hlsl
[numthreads(1, 1, 1)]
[shader("amplification")]
void as_main() { DispatchMesh(32, 1, 1); }
```

## Options

none

## Fix availability

**suggestion** — The grid dimensions are workload-dependent.
