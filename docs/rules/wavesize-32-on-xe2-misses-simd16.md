---
id: wavesize-32-on-xe2-misses-simd16
category: xe2
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# wavesize-32-on-xe2-misses-simd16

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A compute / mesh / amplification kernel pinned `[WaveSize(32)]` under the
`[experimental.target = xe2]` config gate.

## Why it matters on a GPU

Intel Xe2 / Battlemage SIMD16 native execution saves one address-gen cycle
per dispatch over SIMD32. Per Chips and Cheese's Battlemage architecture
deep-dive, kernels pinned to SIMD32 hide native efficiency on Xe2 -- the
hardware can issue SIMD16 in the same throughput tier as SIMD32 but with
lower address-generation latency.

## Options

none. Activated only under `[experimental] target = "xe2"`.

## Fix availability

**suggestion** — Use `[WaveSize(16)]` or `[WaveSize(16, 32)]` if the
kernel's lane utilisation allows.

## See also

- Chips and Cheese: Battlemage architecture
