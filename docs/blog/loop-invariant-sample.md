---
title: "loop-invariant-sample: Calls to any texture sampling intrinsic — `Sample`, `SampleLevel`, `SampleGrad`, `SampleBias`, `SampleCmp`, `SampleCmpLevelZero`, `Gather`,…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: loop-invariant-sample
---

# loop-invariant-sample

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/loop-invariant-sample.md](../rules/loop-invariant-sample.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A texture sample is one of the most expensive single operations in a GPU shader, not in ALU cycles but in latency and Texture Memory Unit (TMU) traffic. On AMD RDNA 3 and NVIDIA Ada Lovelace, an `SampleLevel` call with a resident mip costs approximately 100-300 clock cycles of TMU latency (depending on cache state), though this latency can be hidden by the scheduler issuing other independent instructions. If the sample is loop-invariant, issuing it N times across N iterations costs N times the TMU bandwidth and N cache-fill attempts. There is no TMU-level CSE (common subexpression elimination) across loop iterations; each call issues a fresh request. The GPU compiler may decline to hoist the sample out of the loop because it cannot prove that the texture contents have not changed between iterations (aliasing analysis for GPU textures is conservative), or because the VGPR pressure required to hold the result live across iterations is judged higher than the bandwidth saving.

## What the rule fires on

Calls to any texture sampling intrinsic — `Sample`, `SampleLevel`, `SampleGrad`, `SampleBias`, `SampleCmp`, `SampleCmpLevelZero`, `Gather`, `GatherRed`, `GatherGreen`, `GatherBlue`, `GatherAlpha`, `Load` — inside a loop body when the texture argument, sampler argument, and all coordinate arguments are loop-invariant: none of them depend on the loop induction variable or any value defined inside the loop. The rule fires when the UV or load coordinate is determined by the data-flow graph to be loop-invariant (no transitive dependency on the loop counter or any variable assigned inside the loop). It does not fire when any argument — including the mip level for `SampleLevel`, the gradient for `SampleGrad`, or any component of the coordinate — varies with the loop counter.

See the [What it detects](../rules/loop-invariant-sample.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/loop-invariant-sample.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[loop-invariant-sample.md -> Examples](../rules/loop-invariant-sample.md#examples).

## See also

- [Rule page](../rules/loop-invariant-sample.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
