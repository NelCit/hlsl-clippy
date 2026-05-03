---
id: loop-attribute-conflict
category: control-flow
severity: warn
applicability: suggestion
since-version: v0.5.0
phase: 2
language_applicability: ["hlsl", "slang"]
---

# loop-attribute-conflict

> **Status:** shipped (Phase 2) — see [CHANGELOG](../../CHANGELOG.md).

*(via ADR 0011)*

## What it detects

A `for`, `while`, or `do`-`while` loop whose attribute list contains a contradictory pair of compiler hints — most commonly `[unroll]` together with `[loop]` on the same statement, or `[unroll(N)]` with `[loop]`. The rule also fires on `[unroll(N)]` where `N` exceeds a configurable threshold (`unroll-max`, default 32), because past that bound the unroll either silently degrades to `[loop]` codegen on every back-end or blows up VGPR pressure to the point of dropping wave occupancy. The rule does not fire on a lone `[unroll]`, a lone `[loop]`, a lone `[fastopt]`, or `[unroll(N)]` with `N` at or below the threshold — those are well-formed compiler hints with a single intent.

## Why it matters on a GPU

HLSL's loop attributes are mutually exclusive intent declarations, not composable flags. `[unroll]` tells the compiler to fully replicate the loop body and drop the back-edge entirely; `[loop]` tells the compiler to keep the loop as a real branch and *not* unroll. When both attributes appear on the same loop, DXC and Slang both pick one (typically the first declared, but the rule is not part of the spec) and emit a warning that is easy to miss in a noisy build log. The shader still compiles and runs correctly, but the runtime cost depends on which attribute won — and that choice is fragile across compiler versions and back-ends. The same source on the same hardware can swap between unrolled and rolled codegen across a DXC point release, which is exactly the kind of unowned drift this linter exists to catch.

The hardware impact of the wrong choice is concrete on every IHV. An accidentally-rolled small loop on AMD RDNA 2/3 emits `s_cmp_lg_i32` + `s_cbranch_scc1` per iteration (one SALU compare + one branch); on NVIDIA Turing/Ada it is `ISETP` + `BRA` per iteration on the SM scheduler. Both eat fetch bandwidth and serialise the iteration's instructions behind the back-edge. Conversely, an accidentally-unrolled loop with high iteration count on RDNA pushes VGPR usage past 64 registers per wave, which on RDNA 3 drops occupancy from 16 waves/SIMD to 8 — halving the latency-hiding budget for memory accesses in the same shader. NVIDIA Ada has the same cliff at the 32 / 64 / 80 / 128 register-count steps. Intel Xe-HPG's GRF allocation has its own discrete steps. The difference between the two compiler picks is not a microoptimisation: it is a 2x occupancy swing.

The `unroll-max` threshold check guards the second mode of failure: `[unroll(64)]` on a non-trivial body. The author wrote `64` because the loop bound was `64`, but the unrolled body may exceed the back-end's per-function instruction-count threshold (DXC has a documented loop-unroll body-size limit that DXIL respects), at which point the compiler silently re-rolls the loop and the `[unroll(64)]` annotation becomes a lie that no diagnostic surfaces. The default cap of 32 is conservative — RDNA 3 typically tolerates 64-iteration unrolls of small bodies, NVIDIA Ada similarly — but the right number depends on the loop body's instruction count, and the rule asks the developer to make the choice explicitly rather than letting the compiler decide silently. A higher threshold is appropriate for shaders targeting hardware with abundant register files; a lower threshold is appropriate when occupancy is already tight.

## Examples

### Bad

```hlsl
// Two contradictory hints on the same loop; the compiler silently picks one.
[unroll]
[loop]
for (int i = 0; i < 16; ++i) {
    sum += Tex.Sample(BilinSS, uv + (float)i * 0.01);
}

// Above the unroll-max threshold; likely to either explode VGPR usage or
// be silently re-rolled by the back-end.
[unroll(128)]
for (int i = 0; i < 128; ++i) {
    accumulate(i);
}
```

### Good

```hlsl
// Pick one intent; let the compiler do exactly what was asked.
[unroll]
for (int i = 0; i < 16; ++i) {
    sum += Tex.Sample(BilinSS, uv + (float)i * 0.01);
}

// Or [loop] if the rolled form is what was wanted:
[loop]
for (int i = 0; i < 16; ++i) {
    sum += Tex.Sample(BilinSS, uv + (float)i * 0.01);
}

// For genuinely large iteration counts, default to [loop] and reach for
// [unroll(N)] with N at or below unroll-max (32 by default) only when the
// body is small enough to fit the back-end's unroll budget.
[unroll(8)]
for (int i = 0; i < 128; i += 16) {
    accumulate_chunk(i);
}
```

## Options

- `unroll-max` (integer, default: 32) — the maximum value of `N` permitted in `[unroll(N)]` before the rule flags the loop as likely-to-degrade. Set to 0 to disable the unroll-magnitude check while keeping the conflicting-pair check active. Raise to 64 or 128 if the target hardware has abundant register space and the loop bodies are small (typical for SDF march loops on Ada / RDNA 3 with simple per-step work). Lower to 16 or 8 if the shader is already register-pressured.

## Fix availability

**suggestion** — Two distinct fixes are offered depending on which sub-rule fired. For the conflicting-pair case (`[unroll]` + `[loop]`), the linter prints both candidate single-attribute rewrites side-by-side because choosing between unroll and loop is an intent decision the linter cannot make for the developer. For the over-threshold `[unroll(N)]` case, the suggested fix replaces `[unroll(N)]` with `[loop]` and prints a one-line note explaining that a partial unroll factor (e.g., `[unroll(8)]`) is the right answer if the developer has measured the cost of full rolling. Neither rewrite is machine-applied because the choice is semantic, not mechanical.

## See also

- Related rule: [small-loop-no-unroll](small-loop-no-unroll.md) — companion rule that flags the *missing* `[unroll]` on a constant-bounded small loop
- Related rule: [branch-on-uniform-missing-attribute](branch-on-uniform-missing-attribute.md) — analogous attribute-hint rule for uniform branches
- Related rule: [vgpr-pressure-warning](vgpr-pressure-warning.md) — surfaces the occupancy cliff that an over-aggressive `[unroll(N)]` can trigger
- HLSL attribute reference: `[unroll]`, `[unroll(N)]`, `[loop]`, `[fastopt]`, `[allow_uav_condition]` in the DirectX HLSL Attributes documentation
- Companion blog post: [control-flow overview](../blog/control-flow-overview.md)

---

[Edit this page](https://github.com/NelCit/shader-clippy/edit/main/docs/rules/loop-attribute-conflict.md)

*© 2026 NelCit, CC-BY-4.0.*
