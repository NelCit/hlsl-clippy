---
title: "live-state-across-traceray"
date: 2026-05-02
author: shader-clippy maintainers
category: memory
tags: [hlsl, performance, memory]
status: stub
related-rule: live-state-across-traceray
---

# live-state-across-traceray

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/live-state-across-traceray.md](../rules/live-state-across-traceray.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

`TraceRay` is not an ordinary function call. The DXR execution model suspends the calling shader and invokes intersection, any-hit, and closest-hit shaders that may themselves call `TraceRay` recursively. The hardware cannot keep the calling shader's register file live during traversal â€” the traversal may take hundreds of cycles and would block the entire SIMD unit if registers were held. Instead, the driver spills every value that the compiler determines is live across the trace to a per-lane ray stack: a private VRAM allocation, typically backed by the same scratch-memory infrastructure as local-array indexing. Each spilled `float4` costs a 128-bit write before the trace and a 128-bit read after it, with VRAM latencies (80-300 cycles per access on RDNA 3 and Turing).

## What the rule fires on

Local variables or intermediate computed values that are (a) assigned before a `TraceRay` or `RayQuery::TraceRayInline` call and (b) read after the call returns, making them live across the trace boundary. The rule fires when the number of such live values â€” weighted by their register footprint in bytes â€” exceeds a configurable byte threshold. It also fires for any individual value larger than 16 bytes (one `float4`) that is live across a trace, because even a single large live value materialises in the per-lane ray stack.

See the [What it detects](../rules/live-state-across-traceray.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/live-state-across-traceray.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[live-state-across-traceray.md -> Examples](../rules/live-state-across-traceray.md#examples).

## See also

- [Rule page](../rules/live-state-across-traceray.md) -- canonical reference + change log.
- [memory overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
