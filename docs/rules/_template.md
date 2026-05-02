---
id: rule-id-here
category: math
severity: warn
applicability: machine-applicable
# `since-version` is the FIRST tagged release that will ship the rule. For
# rules landing in v0.6.x set `v0.6.0`; bump to `v0.7.0` for v0.7-cycle rules
# and so on. Do not invent a `v0.5.x` value retroactively for new rules --
# the v0.5 line is closed.
since-version: v0.6.0
phase: 7
# `references` is REQUIRED for rules landing from v0.8 onward (ADR 0018
# §5 criterion #4). At least 2 entries — primary sources for the GPU
# mechanism the rule detects: IHV architecture guides, HLSL specs,
# DirectX-Specs documents, GDC talks, peer-reviewed microbenchmark
# papers. Existing rules predating v0.8 carry a one-time grandfather
# clause; new rules must clear the bar at PR-review time.
references:
  - title: "Title of primary source #1"
    url: "https://example.invalid/source-1"
  - title: "Title of primary source #2"
    url: "https://example.invalid/source-2"
---

# rule-id-here

<!-- Replace this title with the rule ID. Keep the YAML front-matter above accurate.
     `phase` is the implementation tier the rule belongs to (per ADR 0007 / 0010 /
     0011): 2 = AST-only, 3 = reflection-aware, 4 = control-flow / uniformity-aware,
     7 = IR-level / stretch. -->

> **Pre-v0.6 status:** this rule page is published ahead of the implementation. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool.

<!-- Once the rule ships, replace the banner above with:
> **Status:** shipped (Phase N) — see [CHANGELOG](../../CHANGELOG.md).
-->


## References (REQUIRED for v0.8+ rules)

<!-- Per ADR 0018 §5 criterion #4: every rule shipping from v0.8 onward
     MUST carry at least 2 primary-source citations in the front-matter
     `references:` field. Acceptable sources include:

       * Two of {RDNA / Blackwell / Xe2 / Hopper / Ada} architecture
         guides citing the GPU mechanism the rule detects.
       * One IHV architecture doc + one GDC talk slide.
       * One IHV architecture doc + one peer-reviewed microbenchmark
         paper (e.g. arxiv.org/abs/2512.02189).
       * The HLSL Specs Working Draft / DirectX-Specs document
         counts as one citation when the rule is purely spec-driven.

     "We believe X" is not acceptable. Hand-waved heuristics block at
     PR review.

     Pre-v0.8 rules carry a one-time grandfather clause: existing
     pages may keep `references:` empty or omitted. PRs that *modify*
     a pre-v0.8 rule's mechanism explanation should opportunistically
     add citations.
-->

## What it detects

<!-- One paragraph. Be specific about the source pattern: what call, what literal, what
     structural shape triggers the rule. Name the matched expression explicitly. -->

## Why it matters on a GPU

<!-- 2-3 paragraphs. Root the explanation in hardware. Name architectures (RDNA,
     RDNA 2/3, Turing, Ada, Xe-HPG) and hardware units (transcendental unit, VGPR
     file, shared memory banks, wave occupancy, ALU throughput). Quantify when you
     can (cycle counts, occupancy %, bandwidth MB/s). This section is the seed for
     the companion blog post — it should be substantive enough to stand alone. -->

## Examples

### Bad

```hlsl
// Pattern that triggers the rule.
```

### Good

```hlsl
// Preferred form — what the fix produces.
```

## Options

<!-- Config keys honoured by this rule. Format:
     - `threshold` (integer, default: N) — one-line description.
     If the rule is pure (no options), write: none -->

none

## Fix availability

<!-- One of: machine-applicable | suggestion | none -->

**machine-applicable** — The fix is a pure textual substitution with no observable
semantic change. `hlsl-clippy fix` applies it automatically.

<!-- Or for suggestion:
**suggestion** — A candidate fix is shown but requires verification: [reason]. -->

<!-- Or for none:
**none** — [Reason why no automated fix is offered.] -->

## See also

<!-- Related rules, official docs, blog posts. Use bullet list. -->

- Related rule: [other-rule](other-rule.md)
- HLSL intrinsic reference: [link or description]
- Companion blog post: link to the relevant per-category overview under
  `../blog/<category>-overview.md` if one exists; otherwise leave as
  `_not yet published_` until a per-rule post lands.

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/rule-id-here.md)
