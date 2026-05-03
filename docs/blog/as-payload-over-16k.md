---
title: "as-payload-over-16k"
date: 2026-05-02
author: shader-clippy maintainers
category: mesh
tags: [hlsl, performance, mesh]
status: stub
related-rule: as-payload-over-16k
---

# as-payload-over-16k

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/as-payload-over-16k.md](../rules/as-payload-over-16k.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The amplification-shader payload lives in a per-AS-group LDS-style region that the pipeline reserves at workgroup launch time. On NVIDIA Turing/Ada Lovelace and AMD RDNA 2/3, the AS payload is staged through on-chip memory between AS and the launched mesh-shader workgroups; on Intel Xe-HPG it occupies a per-AS slot in the pipeline scoreboard. The 16 KB cap is the contract every IHV ships, sized to fit comfortably in the on-chip staging buffer on RDNA 2 (the most LDS-constrained of the three).

## What the rule fires on

The `payload` struct passed from an amplification shader (`DispatchMesh`) to its child mesh shaders whose total size exceeds the 16,384-byte (16 KB) D3D12 mesh-pipeline cap. The rule walks the struct definition reachable from the amplification entry's payload parameter, sums field sizes after HLSL packing rules, and fires when the total exceeds the cap. Slang reflection provides the per-field byte offsets directly.

See the [What it detects](../rules/as-payload-over-16k.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/as-payload-over-16k.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[as-payload-over-16k.md -> Examples](../rules/as-payload-over-16k.md#examples).

## See also

- [Rule page](../rules/as-payload-over-16k.md) -- canonical reference + change log.
- [mesh overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
