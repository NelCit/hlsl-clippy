---
id: numwaves-anchored-cap
category: workgroup
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# numwaves-anchored-cap

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `[numthreads(X, Y, Z)]` declaration where `X * Y * Z > 1024` (the current
per-thread-group lane cap). Defensive rule for HLSL Specs proposal 0054
(`numWaves`, under-consideration).

## Why it matters on a GPU

The current per-group lane cap is 1024 across every modern IHV (D3D12 spec
limit). Exceeding it is a hard validator error in DXC -- but the proposal
0054 horizon adds a `numWaves` attribute that may relax this. Until 0054
ships, exceedance is an error worth flagging early.

## Options

none

## Fix availability

**suggestion** — Reduce the dispatch shape or wait for proposal 0054 to
ship.
