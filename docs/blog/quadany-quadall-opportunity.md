---
title: "quadany-quadall-opportunity: A pixel-shader `if (cond)` whose condition is a per-lane (quad-divergent) boolean and whose body…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: quadany-quadall-opportunity
---

# quadany-quadall-opportunity

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/quadany-quadall-opportunity.md](../rules/quadany-quadall-opportunity.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Pixel-shader derivatives (`ddx`, `ddy`, and the implicit derivatives consumed by `Sample`) are computed by differencing values across the 2x2 quad of neighbouring pixels. The hardware requires all four quad lanes to be active — even pixels outside the rendered triangle remain active as "helper lanes" specifically to supply derivative samples. When a per-lane `if` retires some quad lanes (because their condition is false), the derivative inputs from those lanes become undefined: AMD RDNA 2/3 returns implementation-defined values for the derivative tap from a retired lane, NVIDIA Ada produces undefined sampler LOD on the surviving lanes' implicit-derivative samples, and Intel Xe-HPG behaves similarly. The visible artefact is mip-aliasing or seams at the boundary between branch-taken and branch-not-taken regions.

## What the rule fires on

A pixel-shader `if (cond)` whose condition is a per-lane (quad-divergent) boolean and whose body issues at least one derivative-bearing operation — `Sample`, `SampleBias`, `SampleGrad`, `ddx`, `ddy`, `ddx_fine`, `ddy_fine`, or any function call that transitively invokes one. The opportunity is to wrap the condition in `QuadAny(cond)`, so that whenever any lane in the 2x2 quad takes the branch, all four lanes participate as helpers and the derivative ops have valid neighbour samples. Companion (not duplicate) of the locked ADR 0010 rule `quadany-replaceable-with-derivative-uniform-branch` — that rule detects the *opposite* direction (replace `QuadAny` with a derivative-uniform predicate); this rule detects the forward direction (wrap a plain `if` in `QuadAny`).

See the [What it detects](../rules/quadany-quadall-opportunity.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/quadany-quadall-opportunity.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[quadany-quadall-opportunity.md -> Examples](../rules/quadany-quadall-opportunity.md#examples).

## See also

- [Rule page](../rules/quadany-quadall-opportunity.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
