---
title: "omm-allocaterayquery2-non-const-flags: An `AllocateRayQuery2(...)` call (the DXR 1.2 form that takes both static and dynamic ray…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: opacity-micromaps
tags: [hlsl, performance, opacity-micromaps]
status: stub
related-rule: omm-allocaterayquery2-non-const-flags
---

# omm-allocaterayquery2-non-const-flags

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/omm-allocaterayquery2-non-const-flags.md](../rules/omm-allocaterayquery2-non-const-flags.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`AllocateRayQuery2(constFlags, dynFlags)` is the DXR 1.2 split-flag intrinsic that lets the developer pass some ray flags as compile-time constants (which the compiler can pattern-match into specialised RayQuery template instantiations) and others as runtime values (which stay generic). The split exists so the compiler can specialise expensive ray-flag combinations — `RAY_FLAG_FORCE_OPAQUE`, `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`, the OMM flags — at compile time, while leaving cheap conditional flags (`RAY_FLAG_SKIP_TRIANGLES` based on a per-thread mask) for runtime.

## What the rule fires on

An `AllocateRayQuery2(...)` call (the DXR 1.2 form that takes both static and dynamic ray flags) whose first argument — the *constant* ray-flag bundle — is not a compile-time constant. Constant-folding through the AST identifies whether the argument resolves to a literal; the rule fires when it does not.

See the [What it detects](../rules/omm-allocaterayquery2-non-const-flags.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/omm-allocaterayquery2-non-const-flags.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[omm-allocaterayquery2-non-const-flags.md -> Examples](../rules/omm-allocaterayquery2-non-const-flags.md#examples).

## See also

- [Rule page](../rules/omm-allocaterayquery2-non-const-flags.md) -- canonical reference + change log.
- [opacity-micromaps overview](./mesh-dxr-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
