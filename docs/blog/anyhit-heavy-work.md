---
title: "anyhit-heavy-work"
date: 2026-05-02
author: hlsl-clippy maintainers
category: dxr
tags: [hlsl, performance, dxr]
status: stub
related-rule: anyhit-heavy-work
---

# anyhit-heavy-work

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/anyhit-heavy-work.md](../rules/anyhit-heavy-work.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The any-hit shader runs every time the BVH traversal finds a primitive that the ray's bounding test admits as a candidate hit. For a single ray, this can mean dozens or hundreds of any-hit invocations as the traversal walks past intersecting geometry that may or may not be the closest. On AMD RDNA 2/3 with hardware RT, the BVH traversal unit issues an any-hit shader call per candidate primitive in a hardware-managed loop; the per-invocation cost dominates ray throughput when foliage, hair, or alpha-tested geometry is in the BVH. NVIDIA Turing, Ada, and Blackwell similarly invoke any-hit per candidate; the SER (Shader Execution Reordering) hardware on Ada and Blackwell can group similar any-hit invocations across rays for better SIMD efficiency, but the per-call work still scales with candidate count. On Intel Xe-HPG, the RT block dispatches any-hit on every triangle the ray enters, with the same multiplicative cost.

## What the rule fires on

Any-hit shaders (entry points marked `[shader("anyhit")]`) that perform work beyond the supported lightweight tasks: alpha-mask sampling and `IgnoreHit()`/`AcceptHitAndEndSearch()` decisions. The rule flags any-hit bodies containing `for` / `while` loops, more than one `Texture2D::Sample*` call, lighting math (dot products against light directions, BRDF evaluation), `TraceRay` recursion, or substantial scratch-VGPR usage. The same logic should appear in the closest-hit shader where the cost is paid only once per ray.

See the [What it detects](../rules/anyhit-heavy-work.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/anyhit-heavy-work.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[anyhit-heavy-work.md -> Examples](../rules/anyhit-heavy-work.md#examples).

## See also

- [Rule page](../rules/anyhit-heavy-work.md) -- canonical reference + change log.
- [dxr overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
