---
title: "redundant-normalize"
date: 2026-05-02
author: hlsl-clippy maintainers
category: saturate-redundancy
tags: [hlsl, performance, saturate-redundancy]
status: stub
related-rule: redundant-normalize
---

# redundant-normalize

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-normalize.md](../rules/redundant-normalize.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`normalize(v)` expands to `v * rsqrt(dot(v, v))`. For a 3-component vector, the full computation is: one 3-wide dot product (three multiplies and two adds), one `rsqrt`, and three scalar multiplies (or a 3-wide vector multiply). On AMD RDNA 3, `v_rsq_f32` is a transcendental instruction that issues at 1/4 of the standard VALU rate. On NVIDIA Turing and Ada Lovelace, `MUFU.RSQ` similarly occupies the multi-function unit and is not pipelined at full throughput. The entire sequence â€” dot product, `rsqrt`, scale â€” costs roughly 8-10 VALU-equivalent instructions on current GPU hardware.

## What the rule fires on

Calls of the form `normalize(normalize(x))` where the outer `normalize` is applied to a vector that is already unit-length because it is the direct result of an inner `normalize` call. The rule matches the direct nested form and the split-variable form where the result of a `normalize` is stored in an intermediate variable and then passed to a second `normalize`. It does not fire when anything other than a `normalize` result is passed to the outer call, even if the value happens to have unit length at runtime.

See the [What it detects](../rules/redundant-normalize.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-normalize.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-normalize.md -> Examples](../rules/redundant-normalize.md#examples).

## See also

- [Rule page](../rules/redundant-normalize.md) -- canonical reference + change log.
- [saturate-redundancy overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
