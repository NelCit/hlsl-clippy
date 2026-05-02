---
id: groupshared-over-32k-without-attribute
category: sm6_10
severity: warn
applicability: machine-applicable
since-version: v0.8.0
phase: 8
---

# groupshared-over-32k-without-attribute

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Total `groupshared` allocation in a translation unit exceeding the SM 6.10
default of 32 KB without a `[GroupSharedLimit(<bytes>)]` attribute on the
entry point.

## Why it matters on a GPU

Per HLSL Specs proposal 0049 (Accepted), the SM 6.10 default groupshared
cap is 32 KB; exceeding it requires the `[GroupSharedLimit(N)]` attribute.
On SM 6.10 retail this compile-errors; on SM <= 6.9 the LDS allocation is
silently truncated, producing out-of-bounds writes in groupshared. RDNA 3
and Turing/Ada have 64 KB / 100 KB / 128 KB caps respectively that the
attribute opts the kernel into; without it the driver enforces the
conservative ceiling.

## Examples

### Bad

```hlsl
groupshared float lds[9000]; // 36 KB
[numthreads(64, 1, 1)]
void cs_main() { lds[0] = 0; }
```

### Good

```hlsl
groupshared float lds[9000]; // 36 KB
[GroupSharedLimit(65536)]
[numthreads(64, 1, 1)]
void cs_main() { lds[0] = 0; }
```

## Options

none

## Fix availability

**machine-applicable** — `hlsl-clippy fix` inserts
`[GroupSharedLimit(<round-up-to-64KB>)]` before the entry-point
declaration.

## See also

- Related rule: [groupshared-too-large](groupshared-too-large.md)
- HLSL Specs proposal 0049
