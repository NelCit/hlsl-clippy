---
title: "oversized-ray-payload: Ray payload structs — the `inout` parameter on `closesthit`, `anyhit`, and `miss` shaders, and…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: oversized-ray-payload
---

# oversized-ray-payload

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/oversized-ray-payload.md](../rules/oversized-ray-payload.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The DXR runtime stores the payload in a per-lane spill region — the same ray stack that backs `live-state-across-traceray` spills. Every byte of payload is written by the caller before traversal and read back by the caller after the closest-hit, any-hit, or miss shader returns. On NVIDIA Turing and Ada Lovelace, the RT cores execute traversal independently of the SM, but the payload itself lives in the SM's local memory and is touched by the streaming multiprocessor on every shader transition. On AMD RDNA 2/3, the Ray Accelerators perform BVH traversal but the payload sits in scratch backed by the vector memory hierarchy; each 16-byte payload chunk costs a cache-line round trip on the spill and refill.

## What the rule fires on

Ray payload structs — the `inout` parameter on `closesthit`, `anyhit`, and `miss` shaders, and the corresponding payload argument to `TraceRay` — whose total size exceeds a configurable byte threshold (default 32 bytes). The rule walks the struct definition, sums the size of each field after HLSL packing rules (16-byte vector alignment, scalar-tail packing), and reports the offending struct together with the entry points that consume it. The rule additionally flags payloads whose declared `[payload]` size attribute (where present) exceeds the threshold even if individual fields appear small after packing.

See the [What it detects](../rules/oversized-ray-payload.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/oversized-ray-payload.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[oversized-ray-payload.md -> Examples](../rules/oversized-ray-payload.md#examples).

## See also

- [Rule page](../rules/oversized-ray-payload.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
