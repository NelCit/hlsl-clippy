---
title: "groupshared-stride-non-32-bank-conflict: Index expressions into `groupshared` arrays of the form `arr[tid * S + k]`, `arr[gi…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: groupshared-stride-non-32-bank-conflict
---

# groupshared-stride-non-32-bank-conflict

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-stride-non-32-bank-conflict.md](../rules/groupshared-stride-non-32-bank-conflict.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

GPU groupshared / LDS memory exposes 32 parallel banks of 4 bytes each on AMD RDNA 2/3 and on NVIDIA Turing / Ada / Blackwell. A wave of 32 lanes hitting 32 distinct banks completes in one cycle; any collision serialises the access. The conflict factor for a constant stride `S` is `32 / gcd(S, 32)` — so stride 2 yields a 2-way conflict (2x slowdown), stride 4 a 4-way (4x), stride 8 an 8-way (8x), stride 16 a 16-way (16x), and stride 64 a 32-way (32x — same as stride 32, because 64 mod 32 is 0). On Intel Xe-HPG the SLM exposes 16 banks at 4 bytes; the same modular arithmetic applies with a bank count of 16, so a stride-of-16 index is the worst case there but stride-2 / 4 / 8 still partially conflict.

## What the rule fires on

Index expressions into `groupshared` arrays of the form `arr[tid * S + k]`, `arr[gi * S + k]`, `arr[(tid << B) | k]`, or 2D access `arr[i][j]` where the inner-dimension stride `S` is in {2, 4, 8, 16, 64} — that is, any stride sharing a non-trivial GCD with 32. The companion rule [groupshared-stride-32-bank-conflict](groupshared-stride-32-bank-conflict.md) catches the worst case (stride exactly a multiple of 32); this rule catches the partial conflicts that still serialise the wave 2-, 4-, 8-, or 16-way.

See the [What it detects](../rules/groupshared-stride-non-32-bank-conflict.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-stride-non-32-bank-conflict.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-stride-non-32-bank-conflict.md -> Examples](../rules/groupshared-stride-non-32-bank-conflict.md#examples).

## See also

- [Rule page](../rules/groupshared-stride-non-32-bank-conflict.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
