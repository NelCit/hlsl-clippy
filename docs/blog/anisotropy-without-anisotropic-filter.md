---
title: "anisotropy-without-anisotropic-filter"
date: 2026-05-02
author: shader-clippy maintainers
category: texture
tags: [hlsl, performance, texture]
status: stub
related-rule: anisotropy-without-anisotropic-filter
---

# anisotropy-without-anisotropic-filter

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/anisotropy-without-anisotropic-filter.md](../rules/anisotropy-without-anisotropic-filter.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The `MaxAnisotropy` field of a sampler descriptor is consumed by the hardware anisotropic filtering path *only* when the `Filter` selector requests anisotropic filtering. AMD RDNA 2/3 routes anisotropic samples through the TMU's anisotropic footprint estimator, which computes the elongation of the sample footprint in texture space and issues up to `MaxAnisotropy` taps along the major axis. NVIDIA Turing/Ada and Intel Xe-HPG document equivalent anisotropic taps. The field is *ignored* under linear or point filtering: the hardware does not consult `MaxAnisotropy` because the chosen filter does not invoke the anisotropic footprint estimator.

## What the rule fires on

A `SamplerState` (or `SamplerComparisonState`) whose descriptor sets `MaxAnisotropy > 1` while the `Filter` field is not one of the anisotropic filter modes (`D3D12_FILTER_ANISOTROPIC`, `D3D12_FILTER_COMPARISON_ANISOTROPIC`, `D3D12_FILTER_MINIMUM_ANISOTROPIC`, `D3D12_FILTER_MAXIMUM_ANISOTROPIC`). The detector enumerates sampler descriptors via Slang reflection and matches the `MaxAnisotropy` field against the `Filter` field on each. It does not fire when `MaxAnisotropy` is 1 (the trivial default) or when the filter is anisotropic; it fires on the silent-misconfiguration case where `MaxAnisotropy` was set with the intent of enabling AF but the filter selector was left on linear/point.

See the [What it detects](../rules/anisotropy-without-anisotropic-filter.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/anisotropy-without-anisotropic-filter.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[anisotropy-without-anisotropic-filter.md -> Examples](../rules/anisotropy-without-anisotropic-filter.md#examples).

## See also

- [Rule page](../rules/anisotropy-without-anisotropic-filter.md) -- canonical reference + change log.
- [texture overview](./texture-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/shader-clippy/issues).*
