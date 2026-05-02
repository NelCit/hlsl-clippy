---
title: "branch-on-uniform-missing-attribute: `if` statements whose condition is provably dynamically uniform — derived exclusively from `cbuffer` fields,…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: branch-on-uniform-missing-attribute
---

# branch-on-uniform-missing-attribute

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/branch-on-uniform-missing-attribute.md](../rules/branch-on-uniform-missing-attribute.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Modern GPU compilers face a choice when generating code for an `if` statement: emit a real conditional branch instruction, or emit predicated (masked) execution of both arms followed by a select. Without explicit guidance, the compiler uses its own heuristics — typically favouring predication for short arms and branching for long ones, tuned for a specific target GPU and register file model. These heuristics are not always correct for the caller's use case, and they can vary between driver versions.

## What the rule fires on

`if` statements whose condition is provably dynamically uniform — derived exclusively from `cbuffer` fields, `nointerpolation` interpolants, `SV_GroupID` (group-level uniform), or other per-dispatch constants — when the `if` statement is not annotated with the `[branch]` attribute. The rule fires on any `if`, `else if`, or `switch` where the condition expression contains no per-thread-varying terms and where no `[branch]` or `[flatten]` attribute is present. It does not fire when `[branch]` is already present, when `[flatten]` is intentional (the user explicitly requested predication), or when the condition involves non-uniform values such as `SV_DispatchThreadID`, `SV_GroupIndex`, interpolated vertex attributes, or texture reads with varying coordinates.

See the [What it detects](../rules/branch-on-uniform-missing-attribute.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/branch-on-uniform-missing-attribute.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[branch-on-uniform-missing-attribute.md -> Examples](../rules/branch-on-uniform-missing-attribute.md#examples).

## See also

- [Rule page](../rules/branch-on-uniform-missing-attribute.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
