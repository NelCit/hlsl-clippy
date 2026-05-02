---
title: "redundant-unorm-snorm-conversion"
date: 2026-05-02
author: hlsl-clippy maintainers
category: math
tags: [hlsl, performance, math]
status: stub
related-rule: redundant-unorm-snorm-conversion
---

# redundant-unorm-snorm-conversion

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/redundant-unorm-snorm-conversion.md](../rules/redundant-unorm-snorm-conversion.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

A UNORM texture format (`R8_UNORM`, `R8G8B8A8_UNORM`, `R16_UNORM`, â€¦) carries an explicit hardware contract: every sampling operation returns a 32-bit float already normalised to the `[0, 1]` range. The conversion happens in fixed-function silicon â€” on AMD RDNA 2/3 it is folded into the texture filter unit's output formatter, on NVIDIA Turing and Ada Lovelace it is the texture-pipeline's address-mode-and-format converter that runs in parallel with the LERP unit, on Intel Xe-HPG it is the sampler's data-port conversion stage. The cost is zero ALU because no ALU runs: the conversion is a side-effect of the load that the texture unit performs whether the shader asks for it or not. SNORM works the same way except the format converter sign-extends and remaps to `[-1, 1]`.

## What the rule fires on

An explicit fixed-point-to-float scaling expression â€” `* (1.0 / 255.0)`, `/ 255.0`, `* (1.0 / 65535.0)`, `* (1.0 / 127.0)`, `* (2.0 / 255.0) - 1.0`, and the literal-evaluated equivalents `* 0.00392156862745098` and similar â€” applied to the result of a texture `Sample`, `Load`, or `Gather` call, or to a value just unpacked from a packed integer source. The rule matches the literal scaling factors that uniquely identify UNORM (`1/255`, `1/65535`) and SNORM (`2/255 âˆ’ 1`, `1/127`) decoding patterns. It is purely AST-driven on the literal: the Phase 2 rule does not look at the resource binding to confirm the texture is actually UNORM/SNORM-formatted, so it can fire on the rare case where the source is an integer view that genuinely needs the explicit scale. A reflection-aware tightening that confirms the source format is filed as a Phase 3 follow-up in ADR 0011.

See the [What it detects](../rules/redundant-unorm-snorm-conversion.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/redundant-unorm-snorm-conversion.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[redundant-unorm-snorm-conversion.md -> Examples](../rules/redundant-unorm-snorm-conversion.md#examples).

## See also

- [Rule page](../rules/redundant-unorm-snorm-conversion.md) -- canonical reference + change log.
- [math overview](./math-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
