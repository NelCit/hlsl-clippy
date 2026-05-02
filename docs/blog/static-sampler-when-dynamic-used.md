---
title: "static-sampler-when-dynamic-used"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: static-sampler-when-dynamic-used
---

# static-sampler-when-dynamic-used

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/static-sampler-when-dynamic-used.md](../rules/static-sampler-when-dynamic-used.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

D3D12 distinguishes static (immutable, declared in the root signature) from dynamic (descriptor-table or root-descriptor) samplers. Static samplers are baked into the pipeline state at PSO creation, occupy *no* descriptor heap slot, and are pre-resident in the sampler unit on every IHV. Dynamic samplers consume a slot in the sampler descriptor heap (D3D12 caps sampler heaps at 2048 simultaneous descriptors), require a descriptor-table dereference at draw time, and on AMD RDNA 2/3 specifically are loaded into the sampler-state SGPR allocation per wave â€” competing with the rest of the SGPR budget that gates wave occupancy.

## What the rule fires on

A `SamplerState` declared as a dynamic sampler binding (i.e. one that consumes a sampler descriptor slot through a descriptor table or root-descriptor entry) whose state â€” `Filter`, `AddressU/V/W`, `MaxAnisotropy`, `BorderColor`, `MipLODBias`, `MinLOD`, `MaxLOD` â€” never varies across draws or dispatches in any reflection-visible call site. The detector uses Slang reflection to enumerate sampler descriptors, and uses an AST + reflection cross-reference to detect that the sampler is bound through a normal table slot rather than declared as `StaticSampler` in the root signature. **D3D12-relevant:** Vulkan binds samplers through descriptor sets without the static-sampler/heap distinction (immutable samplers exist but are a different mechanism), and Metal manages sampler-state objects through the argument-buffer system; this rule still surfaces a portability concern because the runtime cost of an unnecessary heap-resident sampler shows up as register pressure on every backend.

See the [What it detects](../rules/static-sampler-when-dynamic-used.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/static-sampler-when-dynamic-used.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[static-sampler-when-dynamic-used.md -> Examples](../rules/static-sampler-when-dynamic-used.md#examples).

## See also

- [Rule page](../rules/static-sampler-when-dynamic-used.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
