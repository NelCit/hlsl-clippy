---
title: "hitobject-construct-outside-allowed-stages: A `dx::HitObject::TraceRay`, `dx::HitObject::FromRayQuery`, or `dx::HitObject::MakeMiss` constructor call from a stage other than the SM…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: ser
tags: [hlsl, performance, ser]
status: stub
related-rule: hitobject-construct-outside-allowed-stages
---

# hitobject-construct-outside-allowed-stages

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/hitobject-construct-outside-allowed-stages.md](../rules/hitobject-construct-outside-allowed-stages.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The SER programming model assumes that `dx::HitObject` is constructed at well-defined hardware points where the RT subsystem can hand a coherent traversal record back to the shader. On NVIDIA Ada Lovelace, those points correspond to the SM-side handoff slots that the RT cores expose; the hardware does not present the same handoff in any-hit (where the traversal is suspended mid-leaf, not committed) or intersection (where the procedural primitive's existence is still being evaluated). On future AMD and Intel SER implementations, the same hardware-scoped restriction applies.

## What the rule fires on

A `dx::HitObject::TraceRay`, `dx::HitObject::FromRayQuery`, or `dx::HitObject::MakeMiss` constructor call from a stage other than the SM 6.9 SER spec's allowed set: raygeneration, closest-hit, and miss (with stage-specific restrictions per the spec's table). The rule reads the entry-point stage from Slang reflection and matches each constructor against the spec's allowed-stages set, firing when a call originates from any other stage (any-hit, intersection, callable, compute, mesh, amplification, vertex, pixel, etc.).

See the [What it detects](../rules/hitobject-construct-outside-allowed-stages.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/hitobject-construct-outside-allowed-stages.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[hitobject-construct-outside-allowed-stages.md -> Examples](../rules/hitobject-construct-outside-allowed-stages.md#examples).

## See also

- [Rule page](../rules/hitobject-construct-outside-allowed-stages.md) -- canonical reference + change log.
- [ser overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
