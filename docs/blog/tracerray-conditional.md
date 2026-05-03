---
title: "tracerray-conditional"
date: 2026-05-02
author: shader-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: tracerray-conditional
---

# tracerray-conditional

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/tracerray-conditional.md](../rules/tracerray-conditional.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A `TraceRay` call is the most expensive primitive in any DXR shader. The DXR runtime must spill the caller's live registers to a per-lane stack so the called any-hit, intersection, miss, or closest-hit shaders can run with their own register set, then restore on return. On AMD RDNA 2/3 with hardware ray tracing, the spill happens to the per-lane scratch buffer in VMEM â€” typically 128-256 bytes per lane per active variable, costing one `buffer_store_dword` and one `buffer_load_dword` per VGPR live across the trace. On NVIDIA Turing, Ada, and Blackwell, the SER hardware (or the older non-SER traversal) stages spills through L1$, but the data still has to round-trip out of registers and back. On Intel Xe-HPG with hardware RT, the analogous spill goes through the URB / scratch.

## What the rule fires on

Calls to `TraceRay(...)` (DXR pipeline) or `RayQuery::TraceRayInline(...)` (inline ray queries) placed inside an `if`, `for`, or `while` whose condition is not provably uniform across the wave. The rule fires on conditions derived from per-thread varying inputs (pixel coordinates, dispatch thread IDs, per-pixel sample loads) without an enclosing wave-coherence test, and on cases where the trace call is preceded by a large amount of live-state allocation (textures sampled into registers, normals computed, material parameters built up) that must be spilled across the trace.

See the [What it detects](../rules/tracerray-conditional.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/tracerray-conditional.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[tracerray-conditional.md -> Examples](../rules/tracerray-conditional.md#examples).

## See also

- [Rule page](../rules/tracerray-conditional.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
