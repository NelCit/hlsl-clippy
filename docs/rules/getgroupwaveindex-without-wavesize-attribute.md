---
id: getgroupwaveindex-without-wavesize-attribute
category: sm6_10
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# getgroupwaveindex-without-wavesize-attribute

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

A call to `GetGroupWaveIndex()` or `GetGroupWaveCount()` (SM 6.10, proposal
0048 Accepted) inside a function whose declaration does not carry a
`[WaveSize(N)]` attribute.

## Why it matters on a GPU

The SM 6.10 group-wave-index intrinsics return well-defined values only when
the entry point pins the wave size with `[WaveSize(N)]`. Without that pin,
RDNA may run the dispatch as wave32 or wave64, Turing/Ada always wave32, and
Xe-HPG wave8/16/32. Code that indexes by lane count or wave count silently
changes between IHVs.

## Examples

### Bad

```hlsl
[numthreads(64, 1, 1)]
void cs_main() { uint i = GetGroupWaveIndex(); }
```

### Good

```hlsl
[WaveSize(32)]
[numthreads(64, 1, 1)]
void cs_main() { uint i = GetGroupWaveIndex(); }
```

## Options

none

## Fix availability

**suggestion** — The right wave size depends on the algorithm.

## See also

- Related rule: [wavesize-attribute-missing](wavesize-attribute-missing.md)
- HLSL Specs proposal 0048
- Companion blog post: _not yet published_
