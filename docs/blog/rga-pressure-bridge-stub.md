---
title: "rga-pressure-bridge-stub: Per ADR 0018 §4.3, this is an infrastructure investment, not a rule per se.…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: rdna4
tags: [hlsl, performance, rdna4]
status: stub
related-rule: rga-pressure-bridge-stub
---

# rga-pressure-bridge-stub

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/rga-pressure-bridge-stub.md](../rules/rga-pressure-bridge-stub.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The current `vgpr-pressure-warning` rule is an AST heuristic. AMD's RGA ("Live VGPR Analysis") produces ground-truth per-block VGPR counts after codegen. The bridge is queued for v0.10; this stub keeps the candidate alive in the rule catalog while the infrastructure is being built.

## What the rule fires on

Per ADR 0018 §4.3, this is an infrastructure investment, not a rule per se. Fires once per source compiled under the `[experimental.target = rdna4]` config gate, emitting a `Severity::Note` informational diagnostic that points the developer to the future `tools/rga-bridge` work item.

See the [What it detects](../rules/rga-pressure-bridge-stub.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/rga-pressure-bridge-stub.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[rga-pressure-bridge-stub.md -> Examples](../rules/rga-pressure-bridge-stub.md#examples).

## See also

- [Rule page](../rules/rga-pressure-bridge-stub.md) -- canonical reference + change log.
- [rdna4 overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*

**TODO:** category-overview missing for `rdna4`; linked overview is the closest sibling.