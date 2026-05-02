---
id: sample-use-no-interleave
category: memory
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
---

# sample-use-no-interleave

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A `Texture.Sample*()` call whose result is consumed within the next 3
statements without intervening compute. The default heuristic uses a
3-statement sliding window over the enclosing compound block.

## Why it matters on a GPU

NVIDIA Nsight surfaces this pattern as "Warp Stalled by L1 Long Scoreboard" --
texture fetches without enough work between sample and use cause warp
stalls when the L1 cache misses (~150-300 cycles on Turing/Ada/Blackwell;
~120-200 on RDNA 2-4). Interleaving compute (or other independent work)
between sample and use lets the scheduler hide the latency.

## Examples

### Bad

```hlsl
float4 c = tex.Sample(ss, uv);
return c * 2.0; // immediate use
```

### Good

```hlsl
float4 c = tex.Sample(ss, uv);
float ax = a * b;        // independent compute
float bx = ax + c0;
return c * (ax + bx);    // hidden under sample latency
```

## Options

none

## Fix availability

**suggestion** — Hardware-specific; the rule is heuristic and may have
false-positives.
