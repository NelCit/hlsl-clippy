---
title: "waveops-include-helper-lanes-on-non-pixel"
date: 2026-05-02
author: hlsl-clippy maintainers
category: wave-helper-lane
tags: [hlsl, performance, wave-helper-lane]
status: stub
related-rule: waveops-include-helper-lanes-on-non-pixel
---

# waveops-include-helper-lanes-on-non-pixel

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/waveops-include-helper-lanes-on-non-pixel.md](../rules/waveops-include-helper-lanes-on-non-pixel.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

Helper lanes are a pixel-shader-specific concept. On NVIDIA Turing/Ada Lovelace, when a quad of pixels is partially covered, the rasterizer launches the wave with the uncovered lanes marked as helpers â€” they execute the same shader code so derivatives (`ddx` / `ddy`) can be computed by quad-message-passing, but their stores are masked off. AMD RDNA 2/3 implements the same model. Intel Xe-HPG is identical.

## What the rule fires on

The `[WaveOpsIncludeHelperLanes]` attribute applied to an entry-point function whose stage is not pixel. The SM 6.7 attribute is meaningful only on pixel shaders, where the wave includes helper lanes that exist solely to compute derivatives for adjacent quad lanes. On every other stage there are no helper lanes, so the attribute is at best ignored and at worst a hard validator error. Slang reflection identifies the stage; the rule fires when the attribute appears anywhere outside a `[shader("pixel")]` entry.

See the [What it detects](../rules/waveops-include-helper-lanes-on-non-pixel.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/waveops-include-helper-lanes-on-non-pixel.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[waveops-include-helper-lanes-on-non-pixel.md -> Examples](../rules/waveops-include-helper-lanes-on-non-pixel.md#examples).

## See also

- [Rule page](../rules/waveops-include-helper-lanes-on-non-pixel.md) -- canonical reference + change log.
- [wave-helper-lane overview](./wave-helper-lane-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
