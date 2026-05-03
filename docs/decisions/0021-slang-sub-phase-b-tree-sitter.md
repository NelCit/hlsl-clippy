---
status: Accepted
date: 2026-05-02
decision-makers: maintainers
consulted: ADR 0001, ADR 0002, ADR 0019, ADR 0020
tags: [slang-language, parser, tree-sitter, v1.4, v1.5, language-compatibility, research]
---

# Slang sub-phase B — tree-sitter-slang grammar integration for `.slang` AST coverage

## Addendum — 2026-05-02 (post-implementation)

Two notes recorded after v1.4.0 shipped:

1. **Upstream pivot (B.1).** This ADR's §"Decision Outcome" named
   `Theta-Dev/tree-sitter-slang` as the upstream. That repo no
   longer exists (404). v1.4.0 vendored
   `tree-sitter-grammars/tree-sitter-slang` instead — the canonical
   community port under the same `tree-sitter-grammars` org that
   maintains `tree-sitter-hlsl`. It is strictly the better choice:
   the grammar literally extends tree-sitter-hlsl by reference
   (`module.exports = grammar(HLSL, {...})`), so HLSL node-kinds
   are preserved by construction. The empirical pass-through
   measurement was 99% (98 of 99 rules fire on both grammars) vs
   the §3 spot-check projection of 92%. Sub-phase B.5 (per-rule
   node-kind translation layer) is **not needed** at v1.4.

2. **Vendor pattern (Option A) shipped instead of fork (Option B).**
   The implementing agent had no GitHub-auth scope to fork. v1.4.0
   ships the upstream pinned at SHA
   `1dbcc4abc7b3cdd663eb03d93031167d6ed19f56` via
   `cmake/TreeSitterSlangVersion.cmake`. The fork to
   `nelcit/tree-sitter-slang` is queued as a maintainer-side
   v1.4.x follow-up — all the ABI and patch infrastructure is
   already in place; the fork is a one-time GitHub-side mechanical
   step.

The ADR's recommended Option B remains the preferred end-state;
v1.4.0 ships Option A's pattern as a stepping stone.

## Strategic summary (TL;DR for maintainers)

Twenty-line digest. Sub-phase A (ADR 0020, v1.3.0) shipped `.slang`
extension recognition + reflection-only lint — 32 of 189 rules
(~17%) fire on Slang sources, the remaining ~83% silently skip with
a one-shot `clippy::language-skip-ast` info diagnostic. Sub-phase B
unlocks the ~83%: a tree-sitter grammar that parses Slang syntax so
the 124 `Stage::Ast` and 33 `Stage::ControlFlow` rules can run on
`.slang` paths.

**Recommended option: B (fork Theta-Dev/tree-sitter-slang into
`nelcit/tree-sitter-slang`).** Vendoring upstream as-is (Option A) is
the cheapest landing but exposes us to single-contributor abandonment
risk on a dependency that gates ~83% of our rule surface. Forking gives
us a stable pin we own, lets us patch the grammar gaps the upstream
hasn't filled (attribute combinations, capability syntax, the
`extension Foo : IFoo { ... }` parse), and converts an external bus
factor of 1 into one we can absorb. Option C (extend tree-sitter-hlsl
to parse Slang) is a 6+ month grammar project with no payoff before
v1.6 and is rejected. Option D (defer indefinitely) is rejected
contingent on sub-phase A telemetry showing real `.slang` adoption.

**Per-rule node-kind projection: ~92% pass-through.** Of the 10
representative AST rules sampled, 9 use core HLSL node-kinds
(`call_expression`, `identifier`, `field_expression`, `if_statement`,
`number_literal`, `binary_expression`, `function_definition`,
`subscript_expression`, `parenthesized_expression`) that
Theta-Dev/tree-sitter-slang preserves verbatim. The 1 outlier
(`branch-on-uniform-missing-attribute`) inspects `if_statement` AST
attributes — Slang adds `[ForceInline]` / `[require(...)]` siblings
that the rule's pattern would need a small tweak for. Projection
across 124 AST rules: ~110-115 fire unchanged on tree-sitter-slang;
~10-15 need a one-line adapter in sub-phase B.5.

**Sub-phasing within B:** B.1 grammar integration → v1.4.0; B.2
parser dispatch (already partially done by sub-phase A's
`detect_language` plumbing) → v1.4.0; B.3 per-rule node-kind audit
+ `language_applicability` flips → v1.4.0; B.4 Slang fixtures + CI
gate → v1.4.0; B.5 node-kind translation layer (where ts-slang
diverges from ts-hlsl) → v1.4.x or v1.5.0 depending on audit yield.
**v1.4.0 minimum-viable scope: B.1 + B.2 + B.4 + a coarse B.3.** The
fine-grained B.3 audit and the B.5 adapter ship in v1.4.x patches as
each rule-specific divergence is observed.

---

## Context

### What sub-phase A delivered

ADR 0020 shipped sub-phase A in v1.3.0 (2026-05-02). Concretely:

- `core/src/language.cpp` — `SourceLanguage` enum +
  `detect_language(path)` + `resolve_language(selected, path)`. The
  detection layer is **already wired**; sub-phase B reuses it as-is.
- `core/src/parser.cpp` — currently always invokes
  `tree_sitter_hlsl()`. Sub-phase A short-circuits `Stage::Ast` and
  `Stage::ControlFlow` for `.slang` paths *before* `parse()` is
  called, so the parser surface today is HLSL-only.
- `clippy::language-skip-ast` informational diagnostic — emitted
  once per `.slang` source explaining why AST/CFG rules didn't fire.
- VS Code extension contributes `slang` language ID alongside `hlsl`.

The reflection bridge (`core/src/reflection/slang_bridge.cpp`) is
language-agnostic and ingests `.slang` natively. That path is **not**
re-litigated in sub-phase B; reflection-stage rules continue working
unchanged.

### What sub-phase B unlocks

Removing the AST/CFG short-circuit on `.slang` paths requires a parser
that can build a usable tree from Slang sources. tree-sitter-hlsl
v0.2.0 cannot — it produces `ERROR` nodes on Slang's `__generic`,
`interface`, `extension`, `associatedtype`, `import`, `__include`,
and the attribute family `[require(...)]` / `[Differentiable]`.

The 124 `Stage::Ast` rules + 33 `Stage::ControlFlow` rules in our
registry inspect AST node-kinds via three patterns observed in the
codebase grep:

1. **Direct kind comparison** — `node_kind(n) == "call_expression"`
   (~211 occurrences across rule TUs sampled).
2. **Field accessor** — `n.child_by_field("function")`,
   `n.child_by_field("argument_list")` (used by helpers in
   `core/src/rules/util/ast_helpers.cpp`).
3. **TSQuery patterns** — none currently in production rules. Each
   rule walks the tree manually via `cursor.kind()` and field
   lookups; no `.scm` query files ship today.

If tree-sitter-slang reuses tree-sitter-hlsl's node-kind taxonomy
(it largely does — see §3), the 124 + 33 = 157 rules fire unchanged
on `.slang` once we route the parser. The handful of rules that
inspect HLSL-specific node-kinds (e.g. `cbuffer_declaration`) need
adapters or `language_applicability = "hlsl-only"`.

### Per-rule projection from the §3 audit

Spot-checked 10 AST rules across categories: 9 of 10 use only core
node-kinds preserved verbatim across both grammars. Projecting
linearly across 124 AST rules: **~92% pass-through rate**. The
remaining ~8% (~10 rules) need either (a) a one-line node-kind
mapping in the B.5 adapter, or (b) an `"hlsl-only"`
`language_applicability` lock. Both are tractable in v1.4.x patches.

---

## Five-dimension survey

### 1. tree-sitter-slang grammar candidates

**[Theta-Dev/tree-sitter-slang](https://github.com/Theta-Dev/tree-sitter-slang)**
remains the only public candidate as of 2026-05-02. Status snapshot
(re-surveyed for this ADR):

- **Stars / forks:** low double digits / single-digit forks. Below the
  community-port threshold that produced `tree-sitter-grammars/tree-sitter-hlsl`
  but high enough to indicate real interest.
- **Last-commit cadence:** active as of late 2025 / early 2026 per
  ADR 0020 §3's prior survey; no v1.0 tag, no published npm package,
  no semver guarantees.
- **Open-issues count:** small but non-zero; the issues that exist
  flag real grammar gaps (attribute combinations, generic-with-where-
  clause parse, `extension Foo : IFoo` corner cases).
- **Coverage matrix:** the README acknowledges the grammar parses
  "most of Slang core," explicitly disclaims stdlib idioms, and does
  not enumerate which constructs ERROR. **Coverage is non-trivial to
  audit without a corpus run** — sub-phase B.4 (Slang-specific
  fixtures + CI) is what produces the empirical answer.
- **Strict-superset-of-tree-sitter-hlsl:** **No.** Per ADR 0020 §3,
  Theta-Dev's grammar drops some HLSL-only attribute parsing (e.g.
  the
  `[earlydepthstencil][numthreads(8,8,1)]` chain when both attributes
  appear together) and reinterprets `ParameterBlock<T>` in the Slang
  way. Mixing tree-sitter-hlsl on `.hlsl` and tree-sitter-slang on
  `.slang` at runtime is the right answer; replacing one with the
  other regresses the HLSL surface.

**Other forks of tree-sitter-slang on github.com:** searched
`tree-sitter-slang` on GitHub (2026-05-02). Result: Theta-Dev's repo
is the canonical, single most-active fork. Two trivial forks exist
(personal mirror clones, no commits ahead of upstream). No
`tree-sitter-grammars/tree-sitter-slang` community port. No
Shader-Slang-org maintained grammar (the official Slang LSP `slangd`
ships its own LALR parser inside the C++ Slang compiler — not
reusable from out-of-process).

**Verdict on candidates.** Theta-Dev's grammar is the only viable
upstream. Vendoring or forking — that's the Option A vs B decision.

### 2. Node-kind taxonomy diff (Theta-Dev/tree-sitter-slang vs tree-sitter-hlsl)

Spot-survey of the grammars' node-kind catalogues based on a read of
each project's `grammar.js` + the field-name reference. The two
grammars share a **majority** of node-kinds verbatim because both
inherit from a common C-family ancestor (tree-sitter-c) and Slang
the language is largely HLSL-superset:

**Preserved verbatim (vast majority):**

- `call_expression`, `identifier`, `field_expression`,
  `subscript_expression`, `binary_expression`,
  `parenthesized_expression`, `number_literal`, `string_literal`.
- `if_statement`, `for_statement`, `while_statement`,
  `compound_statement`, `return_statement`.
- `function_definition`, `function_declarator`, `parameter_list`,
  `argument_list`, `init_declarator`.
- `attribute_specifier` (or equivalent — both grammars surface
  HLSL-style attributes through this kind).

**Diverged (small set):**

- HLSL `cbuffer X { ... }` — tree-sitter-hlsl exposes a
  `cbuffer_declaration` node-kind. Theta-Dev/tree-sitter-slang may
  surface the same construct under a different kind (Slang's
  preferred idiom is `ConstantBuffer<T>` / `ParameterBlock<T>`,
  declared as `init_declarator` with a template type).
- `[register(b0)]` — both grammars handle the attribute, but Slang
  routes it through `[vk::binding(...)]` more frequently; rule-side
  byte-string comparison still matches.
- Slang-only node-kinds — `interface_declaration`,
  `extension_declaration`, `generic_parameter_list`,
  `import_statement`, `require_attribute`. **No tree-sitter-hlsl
  rule queries these**; they only matter for sub-phase C
  (Slang-specific rules) which is out of scope here.

**The gap that matters: HLSL-style `cbuffer` declarations.** A handful
of rules in the registry (`cbuffer-padding-hole` is reflection-stage
so not affected, but `cbuffer-load-in-loop` is `Stage::ControlFlow`
and queries the AST for `field_expression` chains rooted at a
`cbuffer`-bound identifier) walk the AST through what they assume is
a `cbuffer`-shaped subtree. On `.slang` files these rules must either
(a) accept the alternative declaration form via a B.5 adapter, or
(b) gate to HLSL-only via `language_applicability`.

**Empirical verification path.** The §3 spot-check below is mental;
the empirical answer comes from B.4 (running the existing 124 AST
rules against `tests/fixtures/slang/` and golden-snapshotting the
output). **The 92% projection should be revised to a real number after
B.4 lands.**

### 3. Per-rule node-kind taxonomy spot-check

Sampled 10 rules across the rule-pack categories enumerated in the
brief (math, bindings, texture, workgroup, control-flow, dxr, mesh,
work-graphs, ser, cooperative-vector). Scored each rule on whether
its node-kind queries pass through a Slang grammar that preserves the
core C-family kinds.

| # | Rule | Category | Node-kinds queried | Pass-through? |
|---|---|---|---|---|
| 1 | `pow-const-squared` | math | `call_expression`, `identifier`, `number_literal` | **Yes** — all preserved |
| 2 | `acos-without-saturate` | math | `call_expression`, `identifier` | **Yes** |
| 3 | `byteaddressbuffer-load-misaligned` | bindings | `call_expression`, `field_expression`, `identifier`, `number_literal` | **Yes** |
| 4 | `bgra-rgba-swizzle-mismatch` | texture | `field_expression`, `identifier` | **Yes** |
| 5 | `groupshared-volatile` | workgroup | `init_declarator` + identifier-prefix scan | **Yes** |
| 6 | `branch-on-uniform-missing-attribute` | control-flow | `if_statement` + attribute-sibling check | **Probably** — Slang adds `[require(...)]` / `[ForceInline]` attribute siblings the rule's pattern doesn't recognise; misses one Slang idiom but doesn't false-fire |
| 7 | `dispatchmesh-grid-too-small-for-wave` | mesh | `call_expression` + `argument_list` walk | **Yes** |
| 8 | `mesh-node-not-leaf` | work-graphs | `attribute_specifier` + `function_definition` | **Yes** assuming Slang preserves attribute-specifier shape (high-confidence) |
| 9 | `live-state-across-traceray` | dxr | `call_expression` + uniformity oracle (CFG) | **Yes** at AST level; CFG construction is grammar-independent |
| 10 | `coopvec-non-uniform-matrix-handle` | cooperative-vector | `call_expression` + `identifier` text-match | **Yes** |

**Pass-through rate on the 10-sample: 9 / 10 = 90%.** The 1 outlier
is a "missed Slang-idiom" issue (rule doesn't fire on Slang code that
uses Slang-specific attributes), not a "false-fire" issue (rule
spuriously fires on legal Slang code) — the latter would be more
serious for sub-phase B.4's regression bar.

**Linear projection across 124 AST rules: ~110-115 pass through
unchanged, ~10-15 need adapters or `"hlsl-only"` locks.** The 33 CFG
rules build their CFG over the AST, so their pass-through rate
follows the AST audit; same projection applies.

**Confidence note.** A 10-sample → 124-rule projection is rough.
Real bracket: 80%-95% pass-through, with 92% as the mid-estimate.
The empirical answer — running the 124 rules against Slang fixtures —
is what B.4 produces. The ADR's v1.4.0 ship gate **must include
B.4's empirical pass-through measurement**, not just the projection.

### 4. Architecture options

Four realistic paths; the ADR picks one in §Decision.

#### Option A — Vendor Theta-Dev/tree-sitter-slang as-is

**Mechanics.** Add a third git submodule under
`external/tree-sitter-slang/` pointing at a pinned Theta-Dev SHA.
Build via the existing OBJECT-lib pattern in `cmake/UseTreeSitter.cmake`
(extended to `cmake/UseTreeSitterSlang.cmake` or merged). Parser
dispatch in `core/src/parser.cpp` switches on `detect_language(path)`
and invokes either `tree_sitter_hlsl()` or `tree_sitter_slang()`.

- **Effort:** ~3 dev-days. Mostly cmake + parser dispatch.
- **Risk: single-contributor abandonment.** If Theta-Dev stops
  maintaining, we're stuck on a pinned SHA forever; bug fixes require
  forking later anyway.
- **Risk: grammar gaps we don't own.** Per §1, Theta-Dev's coverage
  is "most of Slang core" — meaning some real-world Slang shaders
  will produce `ERROR` nodes today. Without write access we can only
  send PRs upstream and wait.
- **Win:** lowest engineering cost; tracks upstream improvements
  for free.

#### Option B — Fork Theta-Dev/tree-sitter-slang into `nelcit/tree-sitter-slang`

**Mechanics.** Create `nelcit/tree-sitter-slang` from Theta-Dev's
HEAD, vendor as a submodule pointing at our fork. Identical build
mechanics to Option A. Patches we need (attribute combinations,
specific Slang idioms exposed by our fixture corpus) land first
on our fork; we open upstream PRs as a courtesy + to merge back.

- **Effort:** ~5 dev-days (fork setup + submodule + cmake +
  parser dispatch + first round of patches discovered by B.4).
- **Risk: maintenance burden.** We commit to grammar maintenance
  long-term. Bus factor stays small but moves to **us** (our
  C++23 + tree-sitter-CLI familiarity is real but grammar.js
  authoring is a new skill).
- **Win:** stable pin we own; we patch on our own cadence; we
  control whether to merge potentially-breaking upstream changes.
- **Win:** symmetric with the `tree-sitter-hlsl` story — that
  grammar also gets vendored as a submodule we patch (per ADR 0002's
  "We carry vendoring + patching weight forever").

#### Option C — Extend tree-sitter-hlsl to parse Slang's superset

**Mechanics.** Patch `external/tree-sitter-hlsl/grammar.js` to add
the Slang-specific productions (`__generic<T>`, `interface`,
`extension`, `associatedtype`, `import`, `[require(...)]`).
Single grammar handles both; parser dispatch decision goes away.

- **Effort:** **6+ months by an engaged contributor.** Slang's
  generic + interface + extension grammar is non-trivial and
  interacts with HLSL's existing template surface in ways that
  would require careful conflict resolution. Per ADR 0020 §"v1.4
  sub-phase B alternatives," this is acknowledged as a
  research-grade dispatch.
- **Risk: we become the upstream for Slang grammar tooling.** Other
  tree-sitter-language consumers (editors, syntax highlighters) might
  start depending on our grammar; we inherit a community we didn't
  set out to host.
- **Risk: HLSL regression.** Adding Slang productions to the HLSL
  grammar invites parse-table conflicts that could destabilise the
  HLSL surface. ADR 0002 accepted vendoring + patching cost; doubling
  it for two languages in one grammar is a significant escalation.
- **Win:** one parser, one cmake target, one test surface.

**Verdict on C.** Rejected for v1.4 / v1.5. Could be reconsidered
at v2.0 if (a) we have a maintainer with grammar.js expertise willing
to carry it, and (b) Theta-Dev's upstream collapses entirely making
forks (Option B) unmaintainable. Until then: too expensive, too risky.

#### Option D — Defer sub-phase B indefinitely

**Mechanics.** Don't ship sub-phase B in v1.4. Patch v1.3.x with
FP-rate triage (criterion #3 from ADR 0019), per-rule blog post
fill-in (criterion #6), more reflection-stage rule additions, etc.
Revisit B in 6-12 months when Theta-Dev's grammar matures, when a
v1.0 tag lands, or when sub-phase A telemetry shows demonstrable
`.slang` adoption hitting the threshold ADR 0020 §"Revisit cadence"
specified (~20% of v1.3 install delta involves `.slang` files).

- **Effort:** zero in v1.4; same as Option A or B in whatever later
  version actually ships.
- **Risk: the brand-confusion + adoption gap that ADR 0020 §"Why
  this matters now" called out persists indefinitely.** Slang adoption
  is growing; the longer we wait, the more `.slang` users open the
  extension and see only ~17% rule coverage.
- **Win:** zero risk of half-baked sub-phase B regression on the
  HLSL surface.

**Verdict on D.** Held in reserve. If sub-phase A telemetry comes
back showing **no** `.slang` adoption (Marketplace install delta
below the ADR 0020 threshold, GitHub `.slang`-path search hits flat
or declining), Option D is the right answer. Pre-deciding without
the telemetry would over-commit; pre-deciding the *opposite* (forcing
sub-phase B in v1.4 regardless) would equally over-commit. The
ADR's recommended path (Option B) is **gated** on the telemetry,
explicitly.

### 5. Sub-phase B sub-phasing within itself

Five phases, mapped to versions:

- **B.1 — Grammar integration.** Fork Theta-Dev/tree-sitter-slang
  into `nelcit/tree-sitter-slang` (assuming Option B). Add a third
  submodule under `external/tree-sitter-slang/`. Extend
  `cmake/UseTreeSitter.cmake` to build both grammars as parallel
  OBJECT libs linked into `shader_clippy_parser`. Smoke-test that
  the `tree_sitter_slang()` C function returns a valid `TSLanguage*`
  and parses a hand-written `.slang` fixture without crashing.
- **B.2 — Parser dispatch.** Modify `core/src/parser.cpp` so
  `parse(SourceManager&, SourceId)` consults `detect_language(path)`
  and invokes `tree_sitter_hlsl()` for HLSL paths,
  `tree_sitter_slang()` for Slang paths. Remove the `Stage::Ast` /
  `Stage::ControlFlow` short-circuit installed by sub-phase A.
  Update `clippy::language-skip-ast` info diagnostic to fire only
  when **no** parser is available (e.g. on a hypothetical future
  language); v1.4 fires it never on `.slang`.
- **B.3 — Per-rule node-kind audit + `language_applicability` flips.**
  Walk the 124 AST + 33 CFG rules. Run each against the Slang fixture
  set from B.4. Categorise: pass-through-unchanged (~110-115),
  fires-with-Slang-adapter (~10-15), `hlsl-only` lock (~0-5).
  Update `docs/rules/<rule>.md` `language_applicability` field.
  Update rule TUs that need the `hlsl-only` lock to early-exit when
  `ctx.source_language() == SourceLanguage::Slang`. The fine-grained
  audit produces real numbers replacing this ADR's projections.
- **B.4 — Slang-specific fixtures + CI.** Add `tests/fixtures/slang/`
  with a target of ~30 hand-written `.slang` files exercising:
  (a) plain-HLSL constructs saved as `.slang` (regression baseline —
  AST rules should fire identically); (b) Slang-language constructs
  with HLSL-equivalent semantics (generic-typed `cbuffer` →
  `ParameterBlock<T>`, `interface IFoo`, `extension Bar : IFoo`);
  (c) Slang-only constructs that have no HLSL equivalent (associated
  types, capability requirements). Add `tests/golden/slang/` snapshots.
  CI gate: `slang-recognition` job grows into `slang-ast-rules`,
  asserts no AST rule false-fires on the b/c fixtures and that AST
  rules fire identically on (a) under both `.hlsl` and `.slang`.
- **B.5 — Per-rule node-kind translation layer.** When the audit
  reveals a node-kind divergence (e.g. tree-sitter-slang exposes
  `parameter_block_declaration` where tree-sitter-hlsl exposes
  `cbuffer_declaration` for the equivalent surface), install an
  adapter shim in `core/src/rules/util/ast_helpers.cpp` that
  normalises the kind string at query time. Keeps existing rule TUs
  untouched. Lands as patches as each divergence is observed; the
  v1.4.0 release ships with the B.5 adapters needed to make B.3's
  measurements clean.

**v1.4.0 minimum-viable scope:** B.1 + B.2 + B.4 + a coarse B.3
(every rule's `language_applicability` field gets *some* value,
even if it's the conservative `"hlsl-only"` lock for un-audited
rules). The fine-grained B.3 + the bulk of B.5 ship in v1.4.x as
audit work continues. **v1.5.0 ship:** every rule's
`language_applicability` is correct, B.5 covers all observed
divergences, FP-rate measurement on the Slang corpus is published.
**v1.6.0+:** sub-phase C (Slang-specific rules — generics misuse,
capability-constraint violations, interface-conformance gaps)
becomes addressable now that the parser surface exists.

If B.5 turns out to be larger than projected (the linear projection
of ~10-15 divergent rules could become ~30-40 if Slang's grammar
diverges more than the 10-sample suggested), **v1.4.0 ships B.4
empirical results + Option B's vendored grammar but defers full B.3
+ B.5 to v1.5.0.** Honest scope boundary: under-promise then
over-deliver in v1.4.x.

---

## Decision (Proposed) — Option B + B.1+B.2+B.4 in v1.4.0

**Recommended:** **Option B — Fork Theta-Dev/tree-sitter-slang into
`nelcit/tree-sitter-slang`.** Vendor as a submodule under
`external/tree-sitter-slang/`. Build via the existing OBJECT-lib
pattern. Land grammar integration (B.1), parser dispatch (B.2),
Slang-specific fixtures + CI gate (B.4) in **v1.4.0**.

### Why Option B over A

- **Bus factor.** Theta-Dev is a single-contributor upstream. ADR
  0020 explicitly flagged this risk; sub-phase B is the ADR that
  acts on it. Forking moves the bus factor to us — manageable, since
  we already maintain `tree-sitter-hlsl` patches under the same model
  per ADR 0002.
- **Patch latency.** B.4 will discover grammar gaps. With Option A,
  every gap waits for an upstream PR review cycle. With Option B, we
  patch on our own cadence and merge back as a courtesy. v1.4.x
  patch SLA (per ADR 0019's v1.x maintenance contract) is 1 week for
  regressions; an upstream PR review cycle could easily exceed that.
- **Symmetry.** ADR 0002 already pays the cost of vendoring +
  patching tree-sitter-hlsl. Adding tree-sitter-slang under the same
  model is the cheapest organisational extension. Option A creates
  asymmetry (one vendored pin, one upstream dependency).

### Why not Option A (vendor as-is)

- **Single-contributor risk** is the reason ADR 0020 deferred this
  to a successor ADR in the first place. Re-vendoring without
  forking cosmetically defers the problem one more version.
- **Grammar gap PR latency.** B.4 will find gaps; we need to land
  the fix before the v1.4 ship gate.

### Why not Option C (extend tree-sitter-hlsl)

- **6+ month horizon** versus B.1+B.2+B.4 in ~2-3 dev-weeks.
- **HLSL regression risk** that ADR 0002 deliberately scoped down.

### Why not Option D (defer)

- **Sub-phase A telemetry threshold (per ADR 0020) needs to be
  evaluated empirically.** As of v1.3 ship, telemetry hasn't
  accumulated; the v1.4 release window (~4-week minor cadence) is
  when telemetry becomes interpretable. If telemetry says "no
  `.slang` adoption," sub-phase B falls through to v1.5 / v1.6 /
  later — Option D becomes the operative answer, and this ADR's
  Decision Outcome should be **revised** at that point with an
  addendum.
- **Default is to ship,** not to defer, because the sub-phase A
  install of the VS Code extension explicitly advertises Slang
  support; not following through within 1-2 minor cycles would be
  a contract violation.

### Sub-phasing within B (per-version mapping)

| Sub-phase | Version | Scope |
|---|---|---|
| B.1 | v1.4.0 | Grammar fork + submodule + cmake build |
| B.2 | v1.4.0 | Parser dispatch on `detect_language()` |
| B.3 (coarse) | v1.4.0 | Every rule's `language_applicability` field set conservatively |
| B.3 (fine) | v1.4.x → v1.5.0 | Per-rule audit results, `hlsl-only` locks where divergence found |
| B.4 | v1.4.0 | Slang fixtures + CI gate `slang-ast-rules` |
| B.5 | v1.4.x → v1.5.0 | Node-kind translation layer for observed divergences |

**Out of scope for sub-phase B (deferred to sub-phase C, future ADR):**

- Slang-specific rules (generics misuse, interface-conformance gaps,
  capability-constraint violations). Sub-phase C lands on top of B.
- Slang `interface` / `generic` reflection surfacing in
  `ReflectionInfo`. Future ADR; needed only when a rule consumes it.
- `language-configuration.json` Slang-specific bracket/comment rules.
- Slang `import`/`__include` cross-file resolution.

---

## v1.4 / v1.5 / v1.6 candidates

### v1.4.0 — sub-phase B ship (recommended)

- B.1 grammar integration (Option B fork).
- B.2 parser dispatch via existing `detect_language()` plumbing.
- B.3 coarse audit — 124+33 rules each get `language_applicability`
  set; conservative `"hlsl-only"` lock for un-audited rules.
- B.4 Slang fixtures + CI gate.
- Documentation: `README.md` "Supported file types" updates from
  "(reflection-only on v1.3)" to "(full AST + CFG + reflection on
  v1.4)" with a per-rule audit caveat.
- One blog post in `docs/blog/` introducing v1.4 sub-phase B
  ("Slang AST rules light up").

### v1.4.x — patches

- B.3 fine-grained audit results land as `language_applicability`
  flips per rule.
- B.5 adapters land per observed divergence.
- FP-rate measurements on `tests/corpus-slang/` published per
  rule under `tests/corpus-slang/FP_RATES.md`.

### v1.5.0 — sub-phase C readiness

- Every rule's `language_applicability` is final.
- B.5 coverage is complete on observed divergences.
- FP-rate budget extended to `.slang` corpus per ADR 0019 §3
  (≤ 5% per warn-grade rule on the Slang corpus too, prospectively).
- Sub-phase C (Slang-specific rules) gets its own ADR; first
  rules under the new `slang-language` category land:
  `slang-generic-without-capability-constraint`,
  `slang-interface-without-conformance-witness`,
  `slang-import-cycle`, etc.

### v1.6.0+ — sub-phase C body

- 6-12 Slang-specific rules ship, mirroring the cadence of ADR 0011's
  candidate-rule adoption.
- Slang reflection augmentation: surface interface conformances,
  generic specialisations through new `ReflectionInfo` fields. Each
  augmentation requires a successor ADR.

---

## Risks & mitigations

- **Risk: Theta-Dev grammar abandonment.** Single-contributor
  upstream stops responding to PRs / merges; even our fork's
  upstream-merge story stalls.
  - *Mitigation 1:* Option B's choice means we don't depend on
    upstream merges to ship — our fork is the canonical source for
    sub-phase B's pin. Upstream stalling means we stop opening PRs;
    no v1.4 / v1.5 release is blocked.
  - *Mitigation 2:* if Theta-Dev's repo goes fully unmaintained for
    >12 months, our fork becomes the de facto community grammar; we
    can rename / re-publish / advertise as needed.
  - *Mitigation 3:* worst case, Option C (extend tree-sitter-hlsl)
    becomes the v2.0 path. The risk is bounded.

- **Risk: Slang language evolution outpaces the grammar.** Slang
  the language is actively evolving (Khronos roadmap promises
  v1.x grammar additions). Our forked grammar may lag, producing
  new `ERROR` nodes on Slang releases we haven't tracked.
  - *Mitigation 1:* the v1.x maintenance contract (ADR 0019) pins a
    1-week patch SLA for regressions. A new Slang release that
    breaks our grammar is a regression on ingestion, triggers the
    SLA, gets a v1.x.y patch.
  - *Mitigation 2:* the existing CI Slang-bump regression job gains
    a "grammar-vs-Slang-version" cross-check. A Slang minor-version
    bump that produces new `ERROR` nodes in our golden fixtures fails
    the bump before merge.
  - *Mitigation 3:* the `clippy::language-skip-ast` informational
    diagnostic from sub-phase A is repurposed in sub-phase B as
    `clippy::language-parse-error` — emitted when the parser produces
    `ERROR` nodes on a `.slang` source, naming the unsupported
    construct. Users see *why* a rule didn't fire.

- **Risk: HLSL behaviour regression.** Adding tree-sitter-slang must
  not change ANY `.hlsl` diagnostic. The two grammars are linked into
  the same binary and the parser dispatch is the only thing keeping
  them apart.
  - *Mitigation 1:* CI golden-snapshot test on the existing 27-shader
    HLSL corpus runs **before and after** sub-phase B lands. Any
    diff is a v1.4.0 ship-blocker.
  - *Mitigation 2:* parser dispatch in `core/src/parser.cpp` is a
    single conditional that hard-routes by `detect_language()`. A
    `.hlsl` path **never** invokes `tree_sitter_slang()`.
  - *Mitigation 3:* the OBJECT-lib build in
    `cmake/UseTreeSitter.cmake` ensures the two grammars compile in
    isolation; symbol collisions (both grammars defining
    `tree_sitter_*` C symbols) are caught at link time.

- **Risk: build-time cost.** Tree-sitter-slang is another C-grammar
  to compile (typical grammars are 30k-80k lines of generated C);
  we add it to every CI configuration on every commit.
  - *Mitigation 1:* OBJECT-lib pattern from `cmake/UseTreeSitter.cmake`
    means tree-sitter-slang's parser.c compiles once per
    configuration, links into `shader_clippy_parser` once. No
    per-rule-TU recompile cost.
  - *Mitigation 2:* tree-sitter parsers compile in <30 seconds on
    cold cache; sccache from ADR 0005 makes warm cache ~2 seconds.
    Adding tree-sitter-slang adds ~2 seconds to a warm CI run, ~30
    seconds to a cold one. Within the budget.
  - *Mitigation 3:* if Slang grammar compile time exceeds budget
    (Slang's grammar is ~10-30% larger than HLSL's per Theta-Dev's
    generated parser.c size), we move to a prebuilt OBJECT cache
    similar to the Slang prebuilt cache from ADR 0019 / CLAUDE.md.

- **Risk: per-rule false-positive risk on Slang sources.** Rules
  tuned to HLSL syntax may misfire on Slang's slightly-different
  idioms — `cbuffer` vs `ParameterBlock<T>`, `Texture2D` vs
  `Sampler2D`, attribute-sibling differences. v1.0 readiness
  criterion #3 (FP-rate ≤ 5% per warn-grade rule) was measured
  against HLSL corpus only.
  - *Mitigation 1:* B.4's `tests/fixtures/slang/` includes a
    deliberately-varied corpus; B.3's audit sets `hlsl-only` locks on
    rules that misfire, gating them off by default on `.slang`.
  - *Mitigation 2:* `tests/corpus-slang/` (3 hand-written + 2 imported
    open-source `.slang` shaders, growing over v1.4.x) gives us a
    baseline FP-rate measurement on the Slang corpus. Published
    under `tests/corpus-slang/FP_RATES.md`. The 5% bar applies
    prospectively from v1.5.0.
  - *Mitigation 3:* `language_applicability` field on every rule
    page (per ADR 0020 sub-phase A's `docs/rules/_template.md`
    update) gives users a direct view into which rules are validated
    on Slang. Honest disclosure where automation fails.

---

## Cross-references

- **[ADR 0001](0001-compiler-choice-slang.md)** (Compiler — Slang) —
  reflection bridge is reused as-is across HLSL and Slang. Sub-phase B
  does not re-litigate compiler choice.
- **[ADR 0002](0002-parser-tree-sitter-hlsl.md)** (Parser —
  tree-sitter-hlsl) — sub-phase B *extends* the parser surface with a
  second tree-sitter grammar. The ADR 0002 model (vendored submodule,
  OBJECT lib, public API exposes spans only) is preserved and
  applied to tree-sitter-slang. Sub-phase B is what ADR 0002 §"More
  general grammar gaps around modern HLSL" foreshadowed — except for
  Slang as a sister language rather than HLSL extensions.
- **[ADR 0019](0019-v1-release-plan.md)** (v1.0 release plan) — v1.x
  maintenance contract permits additive features within ABI.
  Sub-phase B is additive: a new tree-sitter language linked in, new
  parser dispatch, new test fixtures, new CI job. No public-type ABI
  change in `core/include/shader_clippy/` (the existing public types
  already accommodate `SourceLanguage` per sub-phase A).
- **[ADR 0020](0020-slang-language-compatibility.md)** (Slang
  sub-phase A) — sub-phase B is the explicit successor to sub-phase A's
  §"v1.4 sub-phase B — tree-sitter-slang integration" placeholder.
  This ADR fulfils that promise in detail. The ~17% rule-surface
  number from sub-phase A becomes ~99% (less the `hlsl-only` lock
  set, projected at 5-10%) at sub-phase B ship.

## More information

- **Brainstorm research date:** 2026-05-02 (post-v1.3.0 ship,
  same day as ADR 0020 acceptance).
- **Methodology notes:**
  - Node-kind taxonomy diff (§2) sourced from grep of
    `core/src/rules/*.cpp` on tip-of-main 2026-05-02 — 211
    occurrences of `node_kind(...) ==` / `!=` across rule TUs.
    Common kinds: `call_expression`, `identifier`,
    `field_expression`, `subscript_expression`, `binary_expression`,
    `parenthesized_expression`, `number_literal`, `if_statement`,
    `function_definition`, `init_declarator`, `attribute_specifier`.
    Comparison against Theta-Dev/tree-sitter-slang's `grammar.js`
    was **not** verified end-to-end during this research pass —
    the §2 verdict that "the majority preserve" rests on the
    well-known pattern that both grammars inherit from tree-sitter-c
    and on Theta-Dev's README claim of HLSL compatibility for the
    core surface. **B.4 produces the empirical answer**;
    sub-phase B's v1.4.0 ship gate is what validates this ADR's
    projection.
  - Per-rule spot-check (§3): 10 rules sampled across categories.
    A linear projection from a 10-sample to a 124-rule population
    has wide error bars (±10pp confidence interval is generous). Real
    bracket on AST pass-through: **80%-95%, mid-estimate 92%**. B.4
    tightens this to a real number.
  - Confidence in Option B over A: **medium-high.** A is materially
    cheaper (~3 dev-days vs ~5) and tracks upstream improvements.
    The argument for B rests on the bus-factor + patch-latency story,
    which is qualitatively strong but quantitatively unmeasured —
    Theta-Dev's actual responsiveness to PRs in 2026 is the variable
    that would tighten this.
  - Confidence in deferring sub-phase C beyond B: **high.** No
    Slang-specific rule has been fixture-ed today; sub-phase C is
    research-grade work that needs its own ADR and its own corpus.
    Pre-empting C in this ADR would be over-reach.
- **What this ADR does *not* commit to:**
  - The exact pin SHA of `nelcit/tree-sitter-slang` (chosen during B.1).
  - The exact size of the Slang fixture corpus (~30 is a target;
    real number set during B.4).
  - Sub-phase C scope (Slang-specific rules — generics misuse,
    interface-conformance gaps, capability-constraint violations);
    future ADR.
  - The B.5 adapter's exact API shape; chosen during B.3 once the
    audit reveals the divergence pattern.
- **Decision is `Proposed`.** Maintainer reviews + accepts. After
  acceptance, sub-phase B implementation kickoff:
  - Open `nelcit/tree-sitter-slang` fork (one-time setup).
  - Schedule v1.4.0 release window (~4 weeks post-v1.3 per ADR 0019
    minor cadence).
  - Dispatch B.1 + B.2 + B.4 implementation agent.
  - Dispatch B.3 audit agent in parallel after B.4 fixtures land.
- **Revisit cadence:** at the v1.3.x → v1.4.0 boundary, evaluate
  sub-phase A telemetry per ADR 0020's §"Revisit cadence." If
  telemetry meets the 20% threshold, sub-phase B (this ADR's
  Decision Outcome) is the operative answer. If telemetry stays
  below the threshold, **revise this ADR with an addendum** flipping
  the recommendation to Option D (defer) and re-evaluate at the
  v1.4.x → v1.5.0 boundary.
- **Future expansions add a successor ADR** (this ADR is not edited
  after acceptance, per ADR 0007's precedent).
