---
title: "getgroupwaveindex-without-wavesize-attribute"
date: 2026-05-02
author: hlsl-clippy maintainers
category: sm6_10
tags: [hlsl, performance, sm6_10]
status: stub
related-rule: getgroupwaveindex-without-wavesize-attribute
---

# getgroupwaveindex-without-wavesize-attribute

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/getgroupwaveindex-without-wavesize-attribute.md](../rules/getgroupwaveindex-without-wavesize-attribute.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The SM 6.10 group-wave-index intrinsics return well-defined values only when the entry point pins the wave size with `[WaveSize(N)]`. Without that pin, RDNA may run the dispatch as wave32 or wave64, Turing/Ada always wave32, and Xe-HPG wave8/16/32. Code that indexes by lane count or wave count silently changes between IHVs.

## What the rule fires on

A call to `GetGroupWaveIndex()` or `GetGroupWaveCount()` (SM 6.10, proposal 0048 Accepted) inside a function whose declaration does not carry a `[WaveSize(N)]` attribute.

See the [What it detects](../rules/getgroupwaveindex-without-wavesize-attribute.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/getgroupwaveindex-without-wavesize-attribute.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[getgroupwaveindex-without-wavesize-attribute.md -> Examples](../rules/getgroupwaveindex-without-wavesize-attribute.md#examples).

## See also

- [Rule page](../rules/getgroupwaveindex-without-wavesize-attribute.md) -- canonical reference + change log.
- [sm6_10 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
