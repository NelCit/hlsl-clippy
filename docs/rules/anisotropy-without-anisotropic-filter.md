---
id: anisotropy-without-anisotropic-filter
category: texture
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 3
language_applicability: ["hlsl", "slang"]
---

# anisotropy-without-anisotropic-filter

> **Status:** shipped (Phase 3) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `SamplerState` (or `SamplerComparisonState`) whose descriptor sets `MaxAnisotropy > 1` while the `Filter` field is not one of the anisotropic filter modes (`D3D12_FILTER_ANISOTROPIC`, `D3D12_FILTER_COMPARISON_ANISOTROPIC`, `D3D12_FILTER_MINIMUM_ANISOTROPIC`, `D3D12_FILTER_MAXIMUM_ANISOTROPIC`). The detector enumerates sampler descriptors via Slang reflection and matches the `MaxAnisotropy` field against the `Filter` field on each. It does not fire when `MaxAnisotropy` is 1 (the trivial default) or when the filter is anisotropic; it fires on the silent-misconfiguration case where `MaxAnisotropy` was set with the intent of enabling AF but the filter selector was left on linear/point.

## Why it matters on a GPU

The `MaxAnisotropy` field of a sampler descriptor is consumed by the hardware anisotropic filtering path *only* when the `Filter` selector requests anisotropic filtering. AMD RDNA 2/3 routes anisotropic samples through the TMU's anisotropic footprint estimator, which computes the elongation of the sample footprint in texture space and issues up to `MaxAnisotropy` taps along the major axis. NVIDIA Turing/Ada and Intel Xe-HPG document equivalent anisotropic taps. The field is *ignored* under linear or point filtering: the hardware does not consult `MaxAnisotropy` because the chosen filter does not invoke the anisotropic footprint estimator.

The cost of the mismatch is purely the lie in the descriptor. A `MaxAnisotropy = 16` set against a `LINEAR` filter has no runtime cost — the hardware honestly ignores it — but it strongly implies to a reader (and to any tooling that scans descriptors for "16x AF" status) that the texture is being sampled with anisotropic filtering. The visual quality is whatever linear or point filtering produces, and that mismatch surfaces as a "why does this surface look poor at oblique angles?" bug that is hard to track down because the anisotropy field looks right.

The fix is one of: change `Filter` to an anisotropic mode (and accept the AF runtime cost), or set `MaxAnisotropy = 1` to make the descriptor honest. The rule does not pick; it surfaces the inconsistency.

## Examples

### Bad

```hlsl
// Descriptor:
//   Filter        = D3D12_FILTER_MIN_MAG_MIP_LINEAR
//   MaxAnisotropy = 16
// MaxAnisotropy is silently ignored; surface looks like trilinear, not 16xAF.
SamplerState LinearLooksAniso : register(s0);
```

### Good

```hlsl
// Either commit to anisotropic filtering...
// Descriptor:
//   Filter        = D3D12_FILTER_ANISOTROPIC
//   MaxAnisotropy = 16
SamplerState Aniso16 : register(s0);

// ...or make the descriptor consistent with the filter:
// Descriptor:
//   Filter        = D3D12_FILTER_MIN_MAG_MIP_LINEAR
//   MaxAnisotropy = 1
SamplerState TrilinearHonest : register(s1);
```

## Options

none

## Fix availability

**suggestion** — Both directions of the fix have visual or performance consequences (real AF is more expensive than trilinear). The diagnostic surfaces the descriptor inconsistency; the author edits the root signature.

## See also

- Related rule: [mip-clamp-zero-on-mipped-texture](mip-clamp-zero-on-mipped-texture.md) — sampler state that disables a feature the texture supports
- Related rule: [comparison-sampler-without-comparison-op](comparison-sampler-without-comparison-op.md) — sampler descriptor type unused at the call site
- D3D12 reference: `D3D12_SAMPLER_DESC.Filter` and `MaxAnisotropy` interaction in the D3D12 sampler documentation
- Companion blog post: [texture overview](../blog/texture-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/anisotropy-without-anisotropic-filter.md)

*© 2026 NelCit, CC-BY-4.0.*
