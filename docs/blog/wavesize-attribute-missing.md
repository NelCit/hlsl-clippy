---
title: "wavesize-attribute-missing: A compute or amplification entry point that uses wave intrinsics in a way whose…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: workgroup
tags: [hlsl, performance, workgroup]
status: stub
related-rule: wavesize-attribute-missing
---

# wavesize-attribute-missing

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/wavesize-attribute-missing.md](../rules/wavesize-attribute-missing.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Hardware wave size varies across IHVs and even across architectures from the same IHV. AMD RDNA 1/2/3 supports both 32-wide and 64-wide waves; the driver picks one based on the shader's hints (`[WaveSize]`, register pressure, SM version). NVIDIA Turing and Ada Lovelace are always 32-wide warps. Intel Xe-HPG SIMD width is 8, 16, or 32 lanes depending on register pressure and compiler decisions. A shader that relies on a specific wave size — e.g. assumes a wave covers exactly 32 lanes — runs correctly on Turing/Ada but produces silently wrong results on RDNA when the driver picks 64-wide, or on Xe-HPG when the compiler picks 16-wide.

## What the rule fires on

A compute or amplification entry point that uses wave intrinsics in a way whose result depends on the runtime wave size — e.g. `WaveGetLaneCount()` consumed by an arithmetic expression, `WaveReadLaneAt(x, K)` with `K >= 32`, fixed-stride reductions of the form `lane + 32`, or groupshared layouts indexed by `WaveGetLaneCount()` — without a corresponding `[WaveSize(N)]` or `[WaveSize(min, max)]` attribute on the entry. The detector reads the entry's `[WaveSize]` attribute via reflection and scans the AST for wave-size-dependent uses. It does not fire when `[WaveSize]` is present, nor when the only wave intrinsics in use are wave-size-agnostic (`WaveActiveSum`, `WavePrefixSum`, `WaveActiveBitOr`, `WaveActiveAllTrue`).

See the [What it detects](../rules/wavesize-attribute-missing.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/wavesize-attribute-missing.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[wavesize-attribute-missing.md -> Examples](../rules/wavesize-attribute-missing.md#examples).

## See also

- [Rule page](../rules/wavesize-attribute-missing.md) -- canonical reference + change log.
- [workgroup overview](./workgroup-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
