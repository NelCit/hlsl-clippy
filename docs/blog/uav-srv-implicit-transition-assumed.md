---
title: "uav-srv-implicit-transition-assumed"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: uav-srv-implicit-transition-assumed
---

# uav-srv-implicit-transition-assumed

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/uav-srv-implicit-transition-assumed.md](../rules/uav-srv-implicit-transition-assumed.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

D3D12 makes resource state transitions explicit. A resource bound as a UAV in one draw / dispatch and then read as an SRV in the next requires an explicit `D3D12_RESOURCE_BARRIER` (or the new enhanced barrier `D3D12_TEXTURE_BARRIER` on D3D12 Agility SDK 1.7+) between the two â€” the transition flushes the UAV writer's L1 / L0 caches, invalidates the reader's L1, and on AMD RDNA 2/3 specifically issues a wait for the writer's shader-engine to drain before the reader's wave can launch. Without the barrier, the reader sees stale data from before the write (cached in its own L1), partial data (writer not yet drained), or data that lands during the read (race condition). The hardware does not detect the hazard; the runtime does not insert the barrier; the application is responsible for issuing it.

## What the rule fires on

A shader that writes to a UAV `U` and subsequently reads from an SRV `S`, where Slang reflection identifies `U` and `S` as views over the same underlying resource (or aliased GPU virtual address range). The detector enumerates UAV and SRV bindings via reflection, cross-references their backing resource identity (binding metadata, descriptor heap offsets where reflected, or explicit alias annotations), and fires when at least one such pair appears in the reflected binding set with a write-then-read sequence in the AST. **D3D12-relevant:** Vulkan handles the equivalent through `VkImageMemoryBarrier` and explicit pipeline-stage masks at the API level; Metal handles it through `MTLResourceUsage` and command-encoder dependencies; this rule still surfaces a portability concern because every backend requires *some* form of explicit synchronisation between writer and reader and the lint flags the assumption that the runtime will paper over it.

See the [What it detects](../rules/uav-srv-implicit-transition-assumed.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/uav-srv-implicit-transition-assumed.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[uav-srv-implicit-transition-assumed.md -> Examples](../rules/uav-srv-implicit-transition-assumed.md#examples).

## See also

- [Rule page](../rules/uav-srv-implicit-transition-assumed.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
