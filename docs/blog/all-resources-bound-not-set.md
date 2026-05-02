---
title: "all-resources-bound-not-set"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: all-resources-bound-not-set
---

# all-resources-bound-not-set

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/all-resources-bound-not-set.md](../rules/all-resources-bound-not-set.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The `-all-resources-bound` compile flag communicates to the driver that the application guarantees every resource declared in the root signature is bound before any draw or dispatch call. This guarantee unlocks a class of driver-side optimisations that are otherwise disabled for correctness reasons in the general case.

## What the rule fires on

A project-level configuration where shaders are compiled without the `-all-resources-bound` flag (or the equivalent `D3DCOMPILE_ALL_RESOURCES_BOUND` flag in legacy compilation pipelines) despite the project's root signature declaring a fully populated descriptor table â€” one where all declared descriptor ranges are unconditionally bound for every draw or dispatch in the pipeline. The rule operates at project/pipeline level rather than per-shader: it uses Slang's compilation flag introspection to check whether `AllResourcesBound` is set, and cross-references this against the shader's resource declarations to determine whether all declared resources are statically reachable and would be bound in a complete root signature. It fires as a project-level suggestion when the flag is absent and the shader's resource layout is consistent with full population.

See the [What it detects](../rules/all-resources-bound-not-set.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/all-resources-bound-not-set.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[all-resources-bound-not-set.md -> Examples](../rules/all-resources-bound-not-set.md#examples).

## See also

- [Rule page](../rules/all-resources-bound-not-set.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
