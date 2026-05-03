---
title: "discard-then-work"
date: 2026-05-02
author: shader-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: discard-then-work
---

# discard-then-work

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/discard-then-work.md](../rules/discard-then-work.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Pixel shaders execute in 2x2 pixel quads. When one or more pixels in a quad call `discard`, those pixels become helper lanes: they continue executing the shader for the remainder of the shader's work, but their results are never written to the framebuffer. Helper lanes exist because the quad's remaining active pixels still need screen-space derivative information (for `ddx`/`ddy` and for the implicit mip-level selection in `Texture.Sample`). Removing a helper lane prematurely would corrupt the derivatives of the surviving pixels.

## What the rule fires on

Significant computation â€” texture samples, loops with multiple instructions, arithmetic chains longer than a configured threshold â€” that appears on a code path reachable only after a `discard` statement (or `clip(v)` with a potentially-negative argument) whose guard condition is non-uniform (per-pixel varying). The rule fires when the `discard` is inside an `if` whose predicate includes interpolated vertex attributes, texture reads, or other per-pixel varying data, and when the code following the `discard`-containing block is non-trivial. It does not fire when the `discard` is unreachable at runtime (e.g., guarded by a constant condition), when the subsequent work is a single arithmetic expression, or when `[earlydepthstencil]` is present (which changes the discard semantics in a way that makes the subsequent code less hazardous for helpers).

See the [What it detects](../rules/discard-then-work.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/discard-then-work.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[discard-then-work.md -> Examples](../rules/discard-then-work.md#examples).

## See also

- [Rule page](../rules/discard-then-work.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
