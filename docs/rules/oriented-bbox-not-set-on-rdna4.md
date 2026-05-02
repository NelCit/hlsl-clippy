---
id: oriented-bbox-not-set-on-rdna4
category: rdna4
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# oriented-bbox-not-set-on-rdna4

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A defensive informational rule. Fires once per source that contains any RT
call (`TraceRay`, `RayQuery::Proceed`, `TraceRayInline`) under the
`[experimental.target = rdna4]` config gate.

## Why it matters on a GPU

Per Chips and Cheese's RDNA 4 raytracing deep-dive, RDNA 4 gains up to 10%
RT performance when the BLAS is built with the
`D3D12_RAYTRACING_GEOMETRY_FLAG_USE_ORIENTED_BOUNDING_BOX` (or VK
equivalent) flag. The flag is project-side state -- we cannot inspect it
from shader source -- so the rule emits a one-time-per-source informational
note pointing the developer to verify their BLAS-build code.

## Options

none. Activated only under `[experimental] target = "rdna4"`.

## Fix availability

**none** — Project-side configuration cannot be inspected from shader.

## See also

- Chips and Cheese: RDNA 4 raytracing improvements
