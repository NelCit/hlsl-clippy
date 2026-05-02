---
id: ser-coherence-hint-bits-overflow
category: ser
severity: warn
applicability: machine-applicable
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# ser-coherence-hint-bits-overflow

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `MaybeReorderThread(hint, bits)` call where `bits > 16` (or the
`HitObject::MaybeReorderThread` variant where the bits arg > 8). HLSL Specs
proposal 0027 (SER, Accepted) caps the coherence-hint-bits arg.

## Why it matters on a GPU

Values above the cap are silently truncated by the SER scheduler, producing
incoherent reorder. The SER scheduler buckets lanes by the masked low
`hintBits` bits of the hint argument; an overflow widens the input but the
hardware ignores the high bits, defeating the developer's grouping
intent.

## Examples

### Bad

```hlsl
MaybeReorderThread(hint, 24); // 24 > 16
```

### Good

```hlsl
MaybeReorderThread(hint, 8);
```

## Options

none

## Fix availability

**suggestion** — Choose a bit count within spec.
