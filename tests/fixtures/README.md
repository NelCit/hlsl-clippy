# hlsl-clippy test fixtures

Hand-written HLSL fixtures used to validate rule firing.

## Convention

Each expected diagnostic is annotated with a comment on the offending line
(or the line above it):

```hlsl
// HIT(rule-name): one-line reason
float fresnel = pow(1.0 - NdotV, 5.0);
```

A future test runner greps for `// HIT(...)` markers and asserts that the
linter produces a matching diagnostic on that line. Lines with no `HIT`
marker should produce no diagnostic — silent rules are caught by the same
test that asserts the count.

## Layout

- `phase2/` — pure-AST rules (math, redundancy)
- `phase3/` — type / reflection-aware rules (bindings, textures, workgroup, interpolators)
- `phase4/` — control-flow / data-flow rules (divergence, loop-invariance, uniformity)
- `clean.hlsl` — realistic shader fragment that should produce **zero** diagnostics
  across all phases. Negative baseline.

## Notes

- These are hand-crafted to exercise rules. They're not minimal repros from
  real shaders — that's `tests/corpus/`.
- Files must be valid HLSL (Slang must parse them). When in doubt, run
  `slangc -profile sm_6_6 fixture.hlsl` to validate.
- Keep fixtures focused: one category per file, ≤ 200 lines each.
