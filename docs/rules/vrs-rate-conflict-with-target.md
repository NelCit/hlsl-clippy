---
id: vrs-rate-conflict-with-target
category: vrs
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# vrs-rate-conflict-with-target

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A pixel shader that emits `SV_ShadingRate` when the source also declares a
per-primitive coarse-rate marker (e.g. `D3D12_SHADING_RATE_COMBINER`,
`PerPrimitive`, `CoarseShadingRate`).

## Why it matters on a GPU

D3D12 / Vulkan VRS rate combiners produce the *minimum* of per-primitive and
per-pixel rates -- conflicting declarations silently override the author's
expectation. When the per-primitive rate is coarser, the per-pixel rate is
ignored on Turing+/Ampere/Ada/Battlemage; when the per-pixel rate is coarser,
the per-primitive rate dominates. The author who sets both rarely intends
both.

## Examples

### Bad

```hlsl
// Pretend a coarse-rate hint is also set CPU-side.
struct PSOut { float4 c : SV_Target; uint r : SV_ShadingRate; };
PSOut ps_main() { PSOut o; o.r = 0; return o; }
```

## Options

none

## Fix availability

**suggestion** — The combiner choice is application-specific.
