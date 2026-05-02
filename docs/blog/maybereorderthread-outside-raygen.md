---
title: "maybereorderthread-outside-raygen: A call to `dx::MaybeReorderThread(...)` from any DXR stage other than raygeneration. The SM 6.9…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: maybereorderthread-outside-raygen
---

# maybereorderthread-outside-raygen

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/maybereorderthread-outside-raygen.md](../rules/maybereorderthread-outside-raygen.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

SER's value comes from coalescing divergent lanes within a wave around a common shader execution. On NVIDIA Ada Lovelace's RT subsystem, the reorder happens at a hardware-scoped boundary that the runtime can only manipulate at the raygen invocation point — once a wave has dispatched into a closest-hit shader, the lane mapping is committed and reordering would have to spill and re-form the wave, which costs more than it saves. AMD RDNA 4 (when SER ships there) and the cross-platform Vulkan equivalent (`VK_EXT_ray_tracing_invocation_reorder`) exhibit the same constraint for the same hardware reason.

## What the rule fires on

A call to `dx::MaybeReorderThread(...)` from any DXR stage other than raygeneration. The SM 6.9 SER specification restricts `MaybeReorderThread` to raygen because reordering must happen *before* the per-lane work that the reorder is meant to coalesce. Reflection identifies the entry-point stage; the call-graph walk fires when a `MaybeReorderThread` invocation is reachable from a closest-hit, any-hit, miss, callable, or compute entry.

See the [What it detects](../rules/maybereorderthread-outside-raygen.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/maybereorderthread-outside-raygen.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[maybereorderthread-outside-raygen.md -> Examples](../rules/maybereorderthread-outside-raygen.md#examples).

## See also

- [Rule page](../rules/maybereorderthread-outside-raygen.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
