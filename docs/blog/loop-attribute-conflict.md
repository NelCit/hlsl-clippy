---
title: "loop-attribute-conflict"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: loop-attribute-conflict
---

# loop-attribute-conflict

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/loop-attribute-conflict.md](../rules/loop-attribute-conflict.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

HLSL's loop attributes are mutually exclusive intent declarations, not composable flags. `[unroll]` tells the compiler to fully replicate the loop body and drop the back-edge entirely; `[loop]` tells the compiler to keep the loop as a real branch and *not* unroll. When both attributes appear on the same loop, DXC and Slang both pick one (typically the first declared, but the rule is not part of the spec) and emit a warning that is easy to miss in a noisy build log. The shader still compiles and runs correctly, but the runtime cost depends on which attribute won â€” and that choice is fragile across compiler versions and back-ends. The same source on the same hardware can swap between unrolled and rolled codegen across a DXC point release, which is exactly the kind of unowned drift this linter exists to catch.

## What the rule fires on

A `for`, `while`, or `do`-`while` loop whose attribute list contains a contradictory pair of compiler hints â€” most commonly `[unroll]` together with `[loop]` on the same statement, or `[unroll(N)]` with `[loop]`. The rule also fires on `[unroll(N)]` where `N` exceeds a configurable threshold (`unroll-max`, default 32), because past that bound the unroll either silently degrades to `[loop]` codegen on every back-end or blows up VGPR pressure to the point of dropping wave occupancy. The rule does not fire on a lone `[unroll]`, a lone `[loop]`, a lone `[fastopt]`, or `[unroll(N)]` with `N` at or below the threshold â€” those are well-formed compiler hints with a single intent.

See the [What it detects](../rules/loop-attribute-conflict.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/loop-attribute-conflict.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[loop-attribute-conflict.md -> Examples](../rules/loop-attribute-conflict.md#examples).

## See also

- [Rule page](../rules/loop-attribute-conflict.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
