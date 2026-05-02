---
title: "groupshared-uninitialized-read"
date: 2026-05-02
author: hlsl-clippy maintainers
category: control-flow
tags: [hlsl, performance, control-flow]
status: stub
related-rule: groupshared-uninitialized-read
---

# groupshared-uninitialized-read

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/groupshared-uninitialized-read.md](../rules/groupshared-uninitialized-read.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Groupshared memory (LDS on AMD, shared memory on NVIDIA, SLM on Intel Xe) is allocated per thread group and retains its value only for the duration of a single group's lifetime. At the start of a group's execution, the contents of its LDS allocation are undefined â€” the hardware does not zero-initialise it. On most implementations, LDS will contain whatever a previous thread group wrote to those addresses before it finished, or potentially hardware-specific reset values. Either way, reading before writing produces a value that is not under programmer control and will vary across hardware, driver versions, and group scheduling order.

## What the rule fires on

Reads from a `groupshared` variable or array element before any thread in the group has executed a write to that location in the current dispatch, and where no `GroupMemoryBarrierWithGroupSync` between a covering write and the read has been established. The rule fires when the first use of a `groupshared` symbol in the shader body (within a `[numthreads]`-annotated function) is a load expression rather than a store, and when no unconditional write precedes the read either in the same thread's code path or guarded by a barrier that covers all threads. It does not fire when the read is preceded by an unconditional write to the same location by the same thread, or when the pattern matches the established initialise-barrier-read idiom (all threads write, barrier, all threads read).

See the [What it detects](../rules/groupshared-uninitialized-read.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/groupshared-uninitialized-read.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[groupshared-uninitialized-read.md -> Examples](../rules/groupshared-uninitialized-read.md#examples).

## See also

- [Rule page](../rules/groupshared-uninitialized-read.md) -- canonical reference + change log.
- [control-flow overview](./control-flow-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
