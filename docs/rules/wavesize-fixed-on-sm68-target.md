---
id: wavesize-fixed-on-sm68-target
category: wave-helper-lane
severity: warn
applicability: suggestion
since-version: v0.3.0
phase: 3
language_applicability: ["hlsl"]
---

# wavesize-fixed-on-sm68-target

> **Pre-v0 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

*(via ADR 0010)*

## What it detects

A fixed-form `[WaveSize(N)]` attribute on a kernel compiled against an SM 6.8 (or later) target, where the SM 6.8 range form `[WaveSize(min, max)]` or three-arg form `[WaveSize(min, preferred, max)]` would let the runtime pick an in-range wave size that better fits the dispatch. Slang reflection provides the target shader model; the rule fires on `[WaveSize(N)]` against SM 6.8+ targets.

## Why it matters on a GPU

The SM 6.6 fixed `[WaveSize(N)]` attribute pins the kernel to exactly one wave size: the runtime will refuse to dispatch the kernel on devices whose available wave sizes don't include N. On AMD RDNA 1/2/3 (which can run wave32 or wave64), pinning to wave64 means the kernel cannot run when the driver's heuristic prefers wave32, and vice versa. NVIDIA Turing/Ada Lovelace is fixed at wave32, so a `[WaveSize(64)]` kernel is unrunnable there. Intel Xe-HPG can run wave8/16/32 depending on register pressure; pinning to a single value forces a sub-optimal allocation.

SM 6.8 added the range form `[WaveSize(min, max)]` and the three-arg form `[WaveSize(min, preferred, max)]` precisely so authors can express "the kernel works on any wave size between 32 and 64; the runtime picks the one that fits". On a target compiled for SM 6.8+, the range form is strictly more flexible than the fixed form for any kernel whose results are wave-size-tolerant — which is most of them.

The rule is suggestion-tier because some kernels genuinely require a specific wave size (a manually-tuned `WaveReadLaneAt(x, K)` that depends on `K < waveSize`, or a hand-rolled scan of width 32 / 64). The diagnostic surfaces the candidate range form and asks the author to confirm whether the kernel tolerates it.

## Examples

### Bad

```hlsl
// SM 6.8 target; pinning to wave64 means the kernel cannot run on Ada (wave32).
[WaveSize(64)]
[numthreads(64, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    uint sum = WaveActiveSum(tid);
    /* ... */
}
```

### Good

```hlsl
// Range form lets the runtime pick wave32 on Ada and wave64 on RDNA 2.
[WaveSize(32, 64)]
[numthreads(64, 1, 1)]
void main(uint tid : SV_DispatchThreadID) {
    uint sum = WaveActiveSum(tid);
    /* ... */
}
```

## Options

none

## Fix availability

**suggestion** — The fix is a one-attribute edit but only safe when the kernel tolerates a wave-size range. The diagnostic emits the candidate replacement.

## See also

- Related rule: [wavesize-attribute-missing](wavesize-attribute-missing.md) — wave intrinsics without `[WaveSize]`
- Related rule: [wavesize-range-disordered](wavesize-range-disordered.md) — companion validation
- Related rule: [wavereadlaneat-constant-non-zero-portability](wavereadlaneat-constant-non-zero-portability.md) — kernels that genuinely require a fixed wave size
- HLSL specification: [SM 6.8 WaveSize range form](https://microsoft.github.io/DirectX-Specs/d3d/HLSL_ShaderModel6_8.html)
- Companion blog post: [wave-helper-lane overview](../blog/wave-helper-lane-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/wavesize-fixed-on-sm68-target.md)

*© 2026 NelCit, CC-BY-4.0.*
