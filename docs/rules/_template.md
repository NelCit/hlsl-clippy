---
id: rule-id-here
category: math
severity: warn
applicability: machine-applicable
since-version: "0.1"
phase: 2
---

# rule-id-here

<!-- Replace this title with the rule ID. Keep the YAML front-matter above accurate. -->

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
- Companion blog post: _not yet published_

---

[Edit this page](https://github.com/NelCit/hlsl-clippy/edit/main/docs/rules/rule-id-here.md)
