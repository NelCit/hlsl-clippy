---
id: linalg-matrix-element-type-mismatch
category: linalg
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# linalg-matrix-element-type-mismatch

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `linalg::*Mul` chain whose matrix element type (e.g. `COMPONENT_TYPE_FLOAT16`,
`COMPONENT_TYPE_FLOAT_E4M3`) is mixed with a high-precision accumulator
(`COMPONENT_TYPE_FLOAT32` / `_FLOAT64`) without an explicit conversion.
Activates only on SM 6.10+ targets.

## Why it matters on a GPU

The matrix-engine fetcher silently widens the matrix's elements to the
accumulator's precision, performing a per-element conversion that costs
throughput on every IHV's matrix engine (Blackwell 5th-gen Tensor Cores,
RDNA 4 AI accelerator, Xe2 XMX, Hopper Tensor Cores). Operations that look
free in code are paid for at the fetcher.

## Examples

### Bad

```hlsl
linalg::MatrixVectorMul(COMPONENT_TYPE_FLOAT16, COMPONENT_TYPE_FLOAT32, ...);
```

### Good

```hlsl
linalg::MatrixVectorMul(COMPONENT_TYPE_FLOAT16, COMPONENT_TYPE_FLOAT16, ...);
```

## Options

none

## Fix availability

**suggestion** — The intended type chain is application-specific.

## See also

- HLSL Specs proposal 0035
- Companion blog post: _not yet published_
