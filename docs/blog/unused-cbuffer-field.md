---
title: "unused-cbuffer-field: A field declared inside a `cbuffer`, `ConstantBuffer<T>`, or user-defined struct used as a constant…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: bindings
tags: [hlsl, performance, bindings]
status: stub
related-rule: unused-cbuffer-field
---

# unused-cbuffer-field

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/unused-cbuffer-field.md](../rules/unused-cbuffer-field.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

An unused cbuffer field wastes constant-cache capacity. The GPU loads the entire cbuffer or the portion covering the declared fields into the constant-data cache (AMD K-cache, NVIDIA L1 constant cache) as a contiguous block. Fields that are never read by the shader still occupy cache lines in that block, evicting data from other cbuffers or from other fields of the same cbuffer that are actively used.

## What the rule fires on

A field declared inside a `cbuffer`, `ConstantBuffer<T>`, or user-defined struct used as a constant buffer template type, that is never read in any shader entry point or function reachable from an entry point in the same compilation unit. The rule uses Slang's reflection API and reachability analysis: it enumerates all cbuffer fields by name and byte offset, then checks each field name against the set of identifiers referenced in the shader's AST. A field that appears in the declaration but never in an expression is flagged. See `tests/fixtures/phase3/bindings.hlsl`, line 36 (`float4 c` in `UnusedFields`) and `tests/fixtures/phase3/bindings_extra.hlsl`, line 24 (`uint DebugChannel` in `DebugCB`).

See the [What it detects](../rules/unused-cbuffer-field.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/unused-cbuffer-field.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[unused-cbuffer-field.md -> Examples](../rules/unused-cbuffer-field.md#examples).

## See also

- [Rule page](../rules/unused-cbuffer-field.md) -- canonical reference + change log.
- [bindings overview](./bindings-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
