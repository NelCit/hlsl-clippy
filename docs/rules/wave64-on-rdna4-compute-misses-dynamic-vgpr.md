---
id: wave64-on-rdna4-compute-misses-dynamic-vgpr
category: rdna4
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# wave64-on-rdna4-compute-misses-dynamic-vgpr

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A compute entry point declared `[WaveSize(64)]` (or `[WaveSize(64, 64)]`)
under the `[experimental.target = rdna4]` config gate.

## Why it matters on a GPU

Per AMD's RDNA 4 deep-dives (Hot Chips 2025; Chips and Cheese RDNA 4), the
new dynamic-VGPR allocation mode is wave32-only -- the per-wave
`s_alloc_vgpr` instruction works only for the wave32 lane width. wave64
compute on RDNA 4 silently misses the per-block occupancy gain that
dynamic-VGPR mode provides over the static allocation on RDNA 3.

## Examples

### Bad

```hlsl
[WaveSize(64)]
[numthreads(64, 1, 1)]
void cs_main() {}
```

### Good

```hlsl
[WaveSize(32)]
[numthreads(64, 1, 1)]
void cs_main() {}
```

## Options

none. Activated only under `[experimental] target = "rdna4"`.

## Fix availability

**suggestion** — Wave choice is workload-dependent.

## See also

- AMD Hot Chips 2025 RDNA 4 deck
- Chips and Cheese: RDNA 4 dynamic VGPR
