---
title: "startvertexlocation-not-vs-input"
date: 2026-05-02
author: hlsl-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: startvertexlocation-not-vs-input
---

# startvertexlocation-not-vs-input

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/startvertexlocation-not-vs-input.md](../rules/startvertexlocation-not-vs-input.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`SV_StartVertexLocation` is the per-draw constant the rasterizer broadcasts to all VS invocations: it is the `BaseVertexLocation` argument of `DrawIndexed*`, materialised as a per-vertex input. On NVIDIA Ada Lovelace and AMD RDNA 2/3, the value is delivered through the shader-input pipeline as a uniform-across-the-wave value; Intel Xe-HPG implements the same path. The hardware delivers it only at VS-input â€” the value has no defined meaning at any later stage because the rasterizer does not propagate it through the inter-stage parameter cache.

## What the rule fires on

A use of the SM 6.8 `SV_StartVertexLocation` (or the analogous `SV_StartInstanceLocation`) system-value semantic anywhere other than as a vertex-shader input parameter. The SM 6.8 spec restricts these semantics to VS-input position only: they expose the per-draw `BaseVertexLocation` / `StartInstanceLocation` value the runtime passes to `Draw*` calls. Putting them on a VS output, a PS input, or a compute parameter is invalid. Slang reflection identifies the parameter's stage role; the rule fires on misplacement.

See the [What it detects](../rules/startvertexlocation-not-vs-input.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/startvertexlocation-not-vs-input.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[startvertexlocation-not-vs-input.md -> Examples](../rules/startvertexlocation-not-vs-input.md#examples).

## See also

- [Rule page](../rules/startvertexlocation-not-vs-input.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
