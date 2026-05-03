---
title: "omm-rayquery-force-2state-without-allow-flag"
date: 2026-05-02
author: shader-clippy maintainers
category: opacity-micromaps
tags: [hlsl, performance, opacity-micromaps]
status: stub
related-rule: omm-rayquery-force-2state-without-allow-flag
---

# omm-rayquery-force-2state-without-allow-flag

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/omm-rayquery-force-2state-without-allow-flag.md](../rules/omm-rayquery-force-2state-without-allow-flag.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

DXR 1.2's Opacity Micromap feature exposes per-triangle opacity tables that the BVH traversal hardware on every supporting IHV (NVIDIA Ada Lovelace, AMD RDNA 4, Intel Xe-HPG with the OMM extension) can evaluate without invoking an any-hit shader. Two ray flags govern the OMM path:

## What the rule fires on

A `RayQuery<RAY_FLAG_FORCE_OMM_2_STATE>` template instantiation, or a `TraceRayInline` call with the equivalent runtime ray flag, in a shader that does not also set `RAY_FLAG_ALLOW_OPACITY_MICROMAPS` somewhere on the trace. The DXR 1.2 Opacity Micromap (OMM) specification requires both flags to coexist â€” `FORCE_OMM_2_STATE` is meaningful only when OMM is allowed in the first place. Constant-folding the `RAY_FLAG_*` argument at the call site makes the check straightforward.

See the [What it detects](../rules/omm-rayquery-force-2state-without-allow-flag.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/omm-rayquery-force-2state-without-allow-flag.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[omm-rayquery-force-2state-without-allow-flag.md -> Examples](../rules/omm-rayquery-force-2state-without-allow-flag.md#examples).

## See also

- [Rule page](../rules/omm-rayquery-force-2state-without-allow-flag.md) -- canonical reference + change log.
- [opacity-micromaps overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
