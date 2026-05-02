---
id: vrs-without-perprimitive-or-screenspace-source
category: vrs
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# vrs-without-perprimitive-or-screenspace-source

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A pixel-shader entry point that emits `SV_ShadingRate` but the source
contains no upstream VRS source (`[earlydepthstencil]`, per-primitive
coarse-rate hint, or screen-space VRS image).

## Why it matters on a GPU

PS-emitted VRS rates without an upstream source are silently ignored on
most IHVs (Turing+, RDNA 2+, Battlemage). The shading-rate signal needs a
combiner pair: per-pixel + per-primitive (or per-pixel + screen-space). A
PS that emits a rate without a peer signal is dead code from the rasterizer's
perspective.

## Examples

### Bad

```hlsl
struct PSOut { float4 c : SV_Target; uint r : SV_ShadingRate; };
PSOut ps_main() { PSOut o; o.r = 0; return o; }
```

### Good

```hlsl
[earlydepthstencil]
struct PSOut { float4 c : SV_Target; uint r : SV_ShadingRate; };
PSOut ps_main() { PSOut o; o.r = 0; return o; }
```

## Options

none

## Fix availability

**suggestion**
