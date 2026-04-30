---
status: Proposed
date: 2026-04-30
deciders: NelCit
tags: [rules, phase-2, implementation-plan, math, redundancy]
---

# Phase 2 implementation plan — AST-only rule pack

## Context and Problem Statement

Phase 0 ships the rule-engine scaffolding plus `pow-const-squared`. Phase 1 lands the declarative tree-sitter query helper, the quick-fix Rewriter, inline suppression, the `.hlsl-clippy.toml` config loader, and two rules (`redundant-saturate`, `clamp01-to-saturate`). Phase 2 fills out the AST-only rule pack: ~25 additional rules across **math simplification**, **saturate / clamp / redundancy**, and **misc** categories.

This ADR is the implementation plan a future implementer (or several parallel implementers, one per category) executes against. It assumes Phase 1's infrastructure is in place: `Rule` interface, `ts::Query` helper, `Rewriter`, `Diagnostic`, `Fix`, suppression filter. No further infrastructure work is in scope.

## Decision Drivers

- Phase 2 rules are small, isolated, parallelizable per category — three implementers can run math, saturate-redundancy, and misc concurrently with no shared-state conflicts beyond the registry.
- "AST-only" is load-bearing: rules that secretly need Slang reflection or flow analysis must be flagged and reclassified now.
- Doc-pages (`docs/rules/_template.md`) require `machine-applicable | suggestion | none` per rule — categorize fixes up front.
- Fixtures in `tests/fixtures/phase2/{math,redundant}.hlsl` cover ~70% of rules already; the rest is listed as extensions.

## Considered Options

1. **Big-bang Phase 2.** One PR, all ~25 rules. Bad: review explosion, one bug blocks everything.
2. **One PR per rule.** Bad: 25 review cycles, shared utilities re-litigated.
3. **Per-category packs (chosen).** math-pack, saturate-redundancy-pack, misc-pack. Each pack is a thematic blog series; shared utilities land once.

## Decision Outcome

**(3) Per-category packs**, behind a small shared-utilities PR. The remaining sections are the implementation plan proper.

---

## 1. Rule inventory

Effort tiers: **S** ≤ 30 LOC, **M** 30–100 LOC, **L** 100+ LOC (rule body only).

**Math simplification (19 rules)**

| # | id | effort | # | id | effort |
|--|--|--|--|--|--|
| M1 | `pow-to-mul` | S | M11 | `manual-step` | S |
| M2 | `pow-base-two-to-exp2` | S | M12 | `manual-smoothstep` | M |
| M3 | `pow-integer-decomposition` | M | M13 | `length-comparison` | S |
| M4 | `inv-sqrt-to-rsqrt` | S | M14 | `manual-mad-decomposition` | M |
| M5 | `lerp-extremes` | S | M15 | `dot-on-axis-aligned-vector` | S |
| M6 | `mul-identity` | S | M16 | `length-then-divide` | S |
| M7 | `sin-cos-pair` | M | M17 | `cross-with-up-vector` | S |
| M8 | `manual-reflect` | M | M18 | `countbits-vs-manual-popcount` | M |
| M9 | `manual-refract` | M | M19 | `firstbit-vs-log2-trick` | M |
| M10 | `manual-distance` | S | | | |

**Saturate / clamp / redundancy (5; R1+R2 already shipped in Phase 1)**

| # | id | effort |
|--|--|--|
| R1 | `redundant-saturate` | S (Phase 1) |
| R2 | `clamp01-to-saturate` | S (Phase 1) |
| R3 | `redundant-normalize` | S |
| R4 | `redundant-transpose` | S |
| R5 | `redundant-abs` (4 sub-patterns) | S |

**Misc (3 rules)**

| # | id | effort |
|--|--|--|
| X1 | `comparison-with-nan-literal` | S |
| X2 | `compare-equal-float` | M |
| X3 | `redundant-precision-cast` | S |

**Phase 2 net new: 25 rules** (27 total minus R1, R2 already shipped). LOC budget: ~975 rule code + ~200 shared utilities + ~250 test glue ≈ **1.5 kLOC of new C++** plus fixtures and docs.

## 2. Implementation order

Shared-utility PR (§4) lands first. Then three parallel PRs:

- **PR A — math-pack** (M1–M19). May split into `math-arith-pack` (M1–M9) and `math-builtin-pack` (M10–M19) if reviewer load is high.
- **PR B — saturate-redundancy-pack** (R3, R4, R5).
- **PR C — misc-pack** (X1, X2, X3).

Each PR brings: `core/src/rules/<id>.cpp`, registry entry, `tests/rules/<id>_test.cpp`, `docs/rules/<id>.md`, fixture additions.

Wall-clock estimate: 1.5–2 weeks dev + 1 week of doc authoring (schedule-critical path).

## 3. Per-rule specs

### Math simplification

#### M1. `pow-to-mul`

- **detection:** ts query — `call_expression` named `pow` with second arg `number_literal` matching `^[234](\.0)?$`.
- **false positives:** non-literal exponent; literal but ≠ 2/3/4 (handled by M3); negative exponent (out of scope).
- **fix:** replace call span with `(@base)*(@base)[*(@base)...]`. **machine-applicable.**
- **type info:** none.
- **fixture:** `math.hlsl:18, :23` covers `pow(x,2)`, `pow(x,3)`. Adequate.
- **effort:** S. Subsumes Phase 0's `pow-const-squared`; rename/alias on landing.

#### M2. `pow-base-two-to-exp2`

- **detection:** ts query — `pow(@base, @exp)` with `@base` literal `^2(\.0)?$`.
- **false positives:** `2.0f` suffix (handle in literal matcher).
- **fix:** `exp2(@exp)`. **machine-applicable.**
- **type info:** none.
- **fixture:** `math.hlsl:12`. Adequate.
- **effort:** S.

#### M3. `pow-integer-decomposition`

- **detection:** same anchor as M1, exponent `5..8`. Cap at 8.
- **false positives:** non-integer (`5.5`); negatives.
- **fix:** **machine-applicable** when `@base` is identifier/field-expr (no side-effects); **suggestion** otherwise (the rewrite duplicates `@base`).
- **type info:** none; "pure base" is a syntactic check via `pure_expr.hpp`.
- **fixture:** `math.hlsl:6, :28`. Adequate.
- **effort:** M.

#### M4. `inv-sqrt-to-rsqrt`

- **detection:** ts query — `binary_expression /` with left `^1(\.0)?$` and right `sqrt(@x)`.
- **false positives:** `2.0/sqrt(x)` — different shape, doesn't match.
- **fix:** `rsqrt(@x)`. **machine-applicable.**
- **type info:** none.
- **fixture:** `math.hlsl:32`. Adequate.
- **effort:** S.

#### M5. `lerp-extremes`

- **detection:** ts query — `lerp(a, b, t)` with `t` literal `0` or `1`.
- **false positives:** none (literal-matched).
- **fix:** `t==0`→`@a`; `t==1`→`@b`. **machine-applicable.**
- **type info:** none.
- **fixture:** `math.hlsl:38, :43`. Adequate.
- **effort:** S.

#### M6. `mul-identity`

- **detection:** ts query — `binary_expression` matching `_*1`, `_+0`, `_*0` (and commutative reorderings).
- **false positives:** `vec*0` may need `(vector_type)(0)` to preserve shape — without reflection we don't know. Mitigate: ship `*1`/`+0` as machine-applicable; ship `*0` as **suggestion**.
- **fix:** drop the identity multiplier/addend; `*0` rewrite is suggestion-only.
- **type info:** for `*0` only — reflection can promote to machine-applicable in Phase 3.
- **fixture:** `math.hlsl:48-52`. Adequate.
- **effort:** S.

#### M7. `sin-cos-pair`

- **detection:** imperative walk of `compound_statement`. Build `(arg-text-hash → kind)` map; pair (sin, cos) on textually-equal arg fires.
- **false positives:** side-effecting args (`sin(rng())`/`cos(rng())`). Restrict to pure args (identifier or field_expression).
- **fix:** **suggestion** — emit `sincos(@arg, &s, &c)`. Suggestion because variable naming may collide.
- **type info:** none.
- **fixture:** `math.hlsl:58-60`. Adequate.
- **effort:** M.

#### M8. `manual-reflect`

- **detection:** AST matcher for `v - 2*dot(n,v)*n` and commutative reorderings; require identifier-text match between the two `n`s and two `v`s.
- **false positives:** different identifiers (`v - 2*dot(m,v)*n`). Constant `2` vs `2.0` — accept both.
- **fix:** **suggestion** — `reflect(@v, @n)`. Suggestion because incident-vs-normal arg order conventions differ.
- **type info:** none.
- **fixture:** `math.hlsl:65`. Adequate.
- **effort:** M.

#### M9. `manual-refract`

- **status:** **deferred to Phase 3 — see §9.** Structural matcher too noisy without type info; rule's value is low (hand-rolled refract is rare).
- **fixture:** none currently; do not add.

#### M10. `manual-distance`

- **detection:** ts query — `length(@a - @b)`.
- **false positives:** `length(a + (-b))` has different shape; fine.
- **fix:** `distance(@a, @b)`. **machine-applicable.**
- **type info:** none.
- **fixture:** `math.hlsl:69`. Adequate.
- **effort:** S.

#### M11. `manual-step`

- **detection:** ts query — `conditional_expression` with `>` cond, `1` consequence, `0` alternative.
- **false positives:** vector ternary; swapped arms (`x>a ? 0 : 1` is `1 - step`). Only fire on scalar; emit suggestion for swapped form.
- **fix:** `step(@a, @x)`. **machine-applicable** (scalar form); **suggestion** (vector / swapped).
- **type info:** none structural; vector check is a swizzle-text heuristic.
- **fixture:** `math.hlsl:74`. Adequate.
- **effort:** S.

#### M12. `manual-smoothstep`

- **detection:** AST matcher for two-statement let-binding form: `n = saturate((t-a)/(b-a)); return n*n*(3 - 2*n);`. Require literal `3` and `2`.
- **false positives:** `n*n*(2 - 3*n)` is *not* smoothstep; fix the constants exactly.
- **fix:** **suggestion** — collapse to `smoothstep(@a, @b, @t)`. Suggestion because surrounding code may reuse `@n`.
- **type info:** none.
- **fixture:** `math.hlsl:79-81`. Adequate.
- **effort:** M.

#### M13. `length-comparison`

- **detection:** ts query — `length(@v) <op> @r` for op in `< > <= >=`.
- **false positives:** negative `@r` flips comparison after `r*r`; we can't see signs without reflection.
- **fix:** **suggestion** — `dot(@v,@v) <op> (@r)*(@r)`. Promote to machine-applicable in Phase 3 once reflection confirms `@r >= 0`.
- **type info:** want sign info from Slang reflection — Phase 3.
- **fixture:** `math.hlsl:85`. Adequate.
- **effort:** S.

#### M14. `manual-mad-decomposition`

- **detection:** scoped AST walk over `compound_statement`; find `t = a*b; result = t + c;` where `t` has exactly one use. The single-use def-use scan is the bulk of the work.
- **false positives:** `t` used elsewhere — don't fire. With `precise` qualifier or strict-IEEE flags, FMA fold may change rounding — emit suggestion.
- **fix:** **suggestion** — `result = mad(@a, @b, @c)` (or `(@a)*(@b)+(@c)`).
- **type info:** none structural; one-use check is text-based within the block.
- **fixture:** **not present.** Add to `math.hlsl`:
  ```hlsl
  float manual_mad(float a, float b, float c) {
      // HIT(manual-mad-decomposition): split mul/add loses FMA fold.
      float t = a * b;
      return t + c;
  }
  ```
- **effort:** M.

#### M15. `dot-on-axis-aligned-vector`

- **detection:** ts query — `dot(@v, floatN(literals...))` (or args swapped) where exactly one literal is `1` and the rest are `0`.
- **false positives:** non-literal constructor args; non-axis vector (`float3(1, 0.5, 0)`).
- **fix:** `@v.x` (or `.y/.z/.w`). **machine-applicable.**
- **type info:** none.
- **fixture:** **not present.** Add `dot(v, float3(1,0,0))` example to `math.hlsl`.
- **effort:** S.

#### M16. `length-then-divide`

- **detection:** ts query — `@v / length(@v)` with text-equal operands.
- **false positives:** different operands; side-effecting `@v` (re-evaluated by `normalize`).
- **fix:** `normalize(@v)`. **machine-applicable** (pure `@v`); **suggestion** (impure).
- **type info:** none; pure-arg check is syntactic.
- **fixture:** **not present.** Add `v / length(v)` example to `math.hlsl`.
- **effort:** S.

#### M17. `cross-with-up-vector`

- **detection:** ts query — `cross(@v, float3(literals))` where the constructor is an axis vector (one `1`, two `0`).
- **false positives:** non-literal; non-axis.
- **fix:** table-driven: `cross(v, float3(0,1,0))` → `float3(-v.z, 0, v.x)`, etc. Six entries (axis × order). **machine-applicable.**
- **type info:** none.
- **fixture:** **not present.** Add up-axis and right-axis cases to `math.hlsl`.
- **effort:** S.

#### M18. `countbits-vs-manual-popcount`

- **detection:** constant-fingerprint match — a `compound_statement` containing the SWAR magic constants `0x55555555`, `0x33333333`, `0x0F0F0F0F`, `0x01010101`. Fire when ≥ 3 are present on the same identifier.
- **false positives:** unrelated code using these constants (rare).
- **fix:** **suggestion** — `countbits(@x)`. Suggestion because surrounding statements may have side effects.
- **type info:** none.
- **fixture:** **not present.** Add textbook 32-bit popcount to `math.hlsl`.
- **effort:** M.

#### M19. `firstbit-vs-log2-trick`

- **detection:** ts query — `(int)log2((float)@x)` nested cast pattern. Approximate "result feeds an integer context" by checking the parent node is an array subscript or integer assignment.
- **false positives:** `log2((float)x)` used as actual log; the surrounding-context heuristic mitigates.
- **fix:** **suggestion** — `firstbithigh(@x)`. Zero-input behaviour differs.
- **type info:** none structural; integer-context check is syntactic.
- **fixture:** **not present.** Add `firstbit_trick` example to `math.hlsl`.
- **effort:** M.

### Saturate / clamp / redundancy

#### R1. `redundant-saturate` (Phase 1) — `saturate(saturate(x))`. Drop outer. machine-applicable. No Phase 2 work.

#### R2. `clamp01-to-saturate` (Phase 1) — `clamp(x,0,1)` → `saturate(x)`. machine-applicable. No Phase 2 work.

#### R3. `redundant-normalize`

- **detection:** ts query — `normalize(normalize(@x))`.
- **fix:** drop outer. **machine-applicable.**
- **type info:** none.
- **fixture:** `redundant.hlsl:26`. Adequate.
- **effort:** S.

#### R4. `redundant-transpose`

- **detection:** ts query — `transpose(transpose(@m))`.
- **false positives:** none meaningful (square matrix).
- **fix:** drop both. **machine-applicable.**
- **type info:** none.
- **fixture:** `redundant.hlsl:31`. Adequate.
- **effort:** S.

#### R5. `redundant-abs`

- **detection:** ts query — `abs(@e)` where `@e` is one of: `@x*@x` (text-equal); `dot(@x,@x)` (text-equal); `saturate(_)`; `length(_)`.
- **false positives:** side-effecting `@x` filtered by text-equality on identifier only.
- **fix:** drop `abs`. **machine-applicable.**
- **type info:** none.
- **fixture:** `redundant.hlsl:36, :41, :46`. **Add** `abs(length(v))` line for the 4th sub-pattern.
- **effort:** S.

### Misc

#### X1. `comparison-with-nan-literal`

- **detection:** ts query — comparison where either operand is `0.0/0.0` or `asfloat(0xFFC00000)`.
- **fix:** **suggestion** — `isnan(@other)`. Suggestion because true/false branch intent needs a human read.
- **type info:** none.
- **fixture:** `math.hlsl:96`. Adequate.
- **effort:** S.

#### X2. `compare-equal-float`

- **detection:** ts query — `==`/`!=` where either operand is a float-shaped literal (decimal point or `f` suffix).
- **false positives:** **high.** Cannot tell `int x == 0` from `float x == 0` without reflection. Heuristic: only fire when literal form is float-shaped (`0.0`, `0.f`); accept the false-negative on `float x; if (x == 0)`.
- **fix:** **suggestion** — `abs(@a - @b) < EPSILON`.
- **type info:** **wants Slang reflection.** Ship heuristic version in Phase 2; precise version is a Phase 3 follow-up.
- **fixture:** `math.hlsl:91`. Adequate.
- **effort:** M.

#### X3. `redundant-precision-cast`

- **detection:** ts query — `(@T1)((@T2)@e)` where outer is FP-wider and inner is integer-narrower. Heuristic: accept the obvious `(float)((int)x)` form.
- **false positives:** intentional truncation; emit suggestion with "did you mean `trunc(@e)` / `floor(@e)`?".
- **fix:** **suggestion** — `trunc(@e)`.
- **type info:** none structural; precision-loss reasoning is syntactic.
- **fixture:** `math.hlsl:101`. Adequate.
- **effort:** S.

## 4. Shared utilities

Land first as a small PR before per-category packs. All under `core/src/rules/util/`.

- **`call_match.hpp`** — `is_call_to(node, "name")`, `nth_arg`, `arg_count`. ~50 LOC. Used by every math rule.
- **`literal_match.hpp`** — predicates: `is_int_literal_eq(node, n)`, `is_fp_literal_eq(node, x, eps)`, `as_int_literal`. Handles `1`, `1.0`, `1.0f`, `1u` uniformly. ~80 LOC. Used by mul-identity, lerp-extremes, all pow-* rules.
- **`pure_expr.hpp`** — `is_side_effect_free(node)`: identifier / field_expression / number_literal / array_subscript-with-const-index. ~40 LOC. Used by sin-cos-pair, length-then-divide, M3, R5.
- **`fix_builder.hpp`** — wraps Phase 1 `Rewriter`: build a `Fix` from a span replacement, optionally paren-wrap to preserve precedence. ~30 LOC.
- **Extensions to `ts_query.hpp`** (already exists from Phase 1): `match_numeric_literal(node, predicate)`, `node_text_equals(a, b)` for the recurring "same identifier in two places" pattern.

Total shared-utility PR: ≈ 200 LOC + their own unit tests.

## 5. Fixture extensions

Existing `tests/fixtures/phase2/{math,redundant}.hlsl` cover ~70% of rules. Additions needed (do **not** modify in this ADR — implementer adds in their PR):

- **`math.hlsl`**: add functions for M14 `manual-mad-decomposition`, M15 `dot-on-axis-aligned-vector`, M16 `length-then-divide`, M17 `cross-with-up-vector` (×2 axes), M18 `countbits-vs-manual-popcount`, M19 `firstbit-vs-log2-trick`. Six new functions, ~50 lines, well within the 200-line cap.
- **`redundant.hlsl`**: add an `abs(length(v))` line for R5's 4th sub-pattern.
- **Recommended:** `tests/fixtures/phase2/clean.hlsl` — realistic shader fragment that produces zero Phase 2 diagnostics. Negative baseline.

## 6. Test conventions

Catch2 v3 (per ADR 0005). One test file per rule under `tests/rules/<rule-id>_test.cpp`.

Each test:
- **Positive case.** Load the fixture; run the linter; assert that lines marked `// HIT(<rule-id>)` (and only those lines) produce the rule's diagnostic. The shared `tests/support/hit_parser.hpp` (Phase 1) parses HIT markers.
- **Negative case.** Inline string with similar-but-non-matching pattern; assert no diagnostic.
- **Fix snapshot.** Apply the fix; golden-compare to `tests/golden/<rule-id>.hlsl`. `--update-goldens` regenerates.

Catch2 tags: `[rule]`, `[<category>]`, `[<rule-id>]` — `--tag '[math]'` runs a single category.

The runner asserts:
1. Every HIT marker fires.
2. Every diagnostic corresponds to a HIT marker (silent failures caught by counting).
3. Fix output matches golden.

## 7. Quick-fix audit

| rule | applicability |
|--|--|
| M1 pow-to-mul | machine-applicable |
| M2 pow-base-two-to-exp2 | machine-applicable |
| M3 pow-integer-decomposition | machine-applicable (pure base) / suggestion (impure) |
| M4 inv-sqrt-to-rsqrt | machine-applicable |
| M5 lerp-extremes | machine-applicable |
| M6 mul-identity | machine-applicable (`*1`/`+0`) / suggestion (`*0`) |
| M7 sin-cos-pair | suggestion |
| M8 manual-reflect | suggestion |
| M9 manual-refract | **deferred (§9)** |
| M10 manual-distance | machine-applicable |
| M11 manual-step | machine-applicable (scalar) / suggestion (vector / swapped) |
| M12 manual-smoothstep | suggestion |
| M13 length-comparison | suggestion (Phase 2) → machine-applicable Phase 3 |
| M14 manual-mad-decomposition | suggestion |
| M15 dot-on-axis-aligned-vector | machine-applicable |
| M16 length-then-divide | machine-applicable (pure) / suggestion (impure) |
| M17 cross-with-up-vector | machine-applicable |
| M18 countbits-vs-manual-popcount | suggestion |
| M19 firstbit-vs-log2-trick | suggestion |
| R3 redundant-normalize | machine-applicable |
| R4 redundant-transpose | machine-applicable |
| R5 redundant-abs | machine-applicable |
| X1 comparison-with-nan-literal | suggestion |
| X2 compare-equal-float | suggestion |
| X3 redundant-precision-cast | suggestion |

**Counts:** 12 machine-applicable, 12 suggestion. M9 deferred.

The pure textual rewrites (drop a call, swap an intrinsic) tend to be machine-applicable; rewrites that introduce a new intrinsic with semantic differences (sincos, smoothstep, mad, popcount) tend to be suggestions.

## 8. Risks

### tree-sitter-hlsl v0.2.0 grammar gaps

ADR 0002 / ROADMAP open question: v0.2.0 fails to parse `cbuffer X : register(b0)` and emits ERROR nodes. **Phase 2 rules don't touch `cbuffer` declarations**, so this gap is not directly load-bearing. However, fixtures containing a `cbuffer` may parse to trees with ERROR siblings; rule queries must continue traversal past ERROR rather than aborting. Phase 1's visitor harness should already do this — confirm before Phase 2 PRs land.

Other gaps to verify:
- Templates (`Texture2D<float4>`, `StructuredBuffer<MyStruct>`) — math fixtures don't use these; corpus shaders will.
- The `conditional_expression` (ternary `?:`) is in the grammar — confirmed by existing pow-const-squared scaffolding.

### False-positive cascades on type-unaware rules

Three rules ship with known precision limits, mitigated by suggestion-only fixes:

- **M13 length-comparison**: without sign info on `@r`, the `r*r` rewrite is unsafe for negative radii. Suggestion-only.
- **M6 mul-identity `*0`**: without vector-shape info, scalar-zero rewrite is unsafe. Suggestion-only for that sub-pattern.
- **X2 compare-equal-float**: cannot distinguish `int` from `float` operands; relies on literal-shape heuristic. Documented false-negative.

### Performance: 25 rules × every AST node × hundreds of fixtures

Tree-sitter queries are compiled once and run via captures — O(N + M). A typical file ≤ 5k AST nodes; 25 rules × 5k = 125k ops/file, sub-millisecond on modern CPU.

The risk is in imperative walks (M7, M14, M8): per-function-body scans bounded at O(statements²); document and don't pre-optimize. If CI rule-engine job exceeds 60s on the v0.2 corpus, profile then. Optimization candidates: cache `text_of(node)` per file; compile all queries into one multi-rule query at registry init.

## 9. Out-of-scope from Phase 2 / reclassifications

- **M9 `manual-refract` — reclassified to Phase 3.** Structural matcher is too noisy without type info; the rule's value is low (hand-rolled refract is rare in shipping shaders). Phase 3's reflection API can validate the IOR scalar argument and the vector type, sharply reducing false positives.
- **M6 `mul-identity *0` sub-pattern** — stays in Phase 2 as **suggestion-only**; promotion to **machine-applicable** waits on Phase 3 vector-shape info. (Whole rule stays in Phase 2.)
- **M13 `length-comparison`** — stays in Phase 2 as **suggestion-only**; promotion to **machine-applicable** waits on Phase 3 sign / range info.
- **X2 `compare-equal-float`** — stays in Phase 2 with the syntactic heuristic; precise version is filed as a Phase 3 follow-up (not a reclassification).
- The Phase 2 ROADMAP did not include any rules secretly needing flow analysis. M14 `manual-mad-decomposition` needs a tiny one-block def-use check, but that's bounded and does not require a CFG.

**Phase 2 net new rule count after reclassification: 24** (25 minus M9).

### Consequences

Good:
- Three implementers can work in parallel after the shared-utilities PR; ~24 rules in 1.5–2 weeks dev time is feasible.
- The applicability audit (§7) lets the doc-page author fill out front-matter without rediscovering each rule's safety profile.
- Fixture coverage is ~70% complete; additions are bounded.

Bad:
- 12/24 rules are suggestion-only — `hlsl-clippy fix` does less heavy lifting than the clippy comparison implies. Phase 3 should re-audit suggestions for promotion.
- One rule (`manual-refract`) doesn't ship in Phase 2; the v0.2 blog series is 24 posts, not 25. Acceptable.

### Confirmation

- ROADMAP.md Phase 2 list reconciled against this plan; future additions add an addendum ADR (or a successor).
- Each rule's PR includes: rule file, registry entry, Catch2 test, doc page, fixture line. Four-artifact check at PR review.
- Shared-utilities PR (§4) lands first and is reviewed independently.

## Pros and Cons of the Options

- **Big-bang Phase 2.** Bad: review explosion; one bug blocks 25 rules.
- **One PR per rule.** Bad: 25 review cycles; shared utilities re-litigated.
- **Per-category packs (chosen).** Good: thematic blog series per pack; shared utilities land once; parallelizes cleanly.

## Links

- ROADMAP.md, "Phase 2 — AST-only rule pack".
- ADR 0002 (parser — tree-sitter-hlsl): grammar gaps caveat.
- ADR 0007 (rule-pack expansion): rule-categorization context.
- `docs/architecture.md`: rule-engine + diagnostic + fix shape.
- `docs/rules/_template.md`: doc-page front-matter convention.
- `tests/fixtures/phase2/{math,redundant}.hlsl`: ground-truth fixtures.
- `tests/fixtures/README.md`: `// HIT(...)` marker convention.
