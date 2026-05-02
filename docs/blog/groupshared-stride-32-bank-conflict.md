---
title: "groupshared-stride-32-bank-conflict: Declarations of `groupshared` arrays accessed with an index expression of the form `tid *…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-stride-32-bank-conflict
---

# groupshared-stride-32-bank-conflict

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-stride-32-bank-conflict.md](../rules/groupshared-stride-32-bank-conflict.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU groupshared / LDS memory is internally organised into 32 parallel banks (sometimes called "channels"). On AMD RDNA 2/3, the LDS hardware has 32 banks; each bank serves one 32-bit word per cycle, and a wave of 32 lanes accessing 32 distinct banks completes in one clock. On NVIDIA Turing, Ada, and Blackwell, shared memory has 32 banks at 4 bytes wide each, and a warp of 32 threads touching 32 distinct banks likewise completes in one clock. On Intel Xe-HPG, the SLM banks are 16-wide but the same conflict mechanic applies. When two or more lanes in the same wave/warp hit the same bank in the same cycle, the hardware serialises the accesses: a 2-way conflict doubles the latency, a 32-way conflict serialises the entire wave to 32 sequential cycles, a full 32x slowdown on that load or store.

## What the rule fires on

Declarations of `groupshared` arrays accessed with an index expression of the form `tid * 32 + k`, `gi * 32 + k`, `tid << 5 | k`, or any constant stride that is a multiple of 32 (32, 64, 128, ...). The same pattern triggers for 2D groupshared arrays declared `[N][32]` (or `[N][64]`) and accessed `Tile[i][j]` — the second dimension being a power-of-two multiple of 32 forces every column access to land in the same LDS bank.

See the [What it detects](../rules/groupshared-stride-32-bank-conflict.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-stride-32-bank-conflict.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-stride-32-bank-conflict.md -> Examples](../rules/groupshared-stride-32-bank-conflict.md#examples).

## See also

- [Rule page](../rules/groupshared-stride-32-bank-conflict.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
