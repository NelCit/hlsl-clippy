# Architecture Decision Records

This directory holds the durable record of significant decisions for `shader-clippy` — the kind of choices that shape the codebase for years and would otherwise be reconstructed from memory after the original context is gone.

## Format

We use [MADR 4.0](https://adr.github.io/madr/) (Markdown Architectural Decision Records). Every ADR is a single Markdown file with YAML front-matter and a fixed body skeleton:

```markdown
---
status: Accepted | Proposed | Deprecated | Superseded by 000X
date: YYYY-MM-DD
deciders: <github handles>
tags: [tag1, tag2]
---

# Short title

## Context and Problem Statement
## Decision Drivers
## Considered Options
## Decision Outcome
### Consequences
### Confirmation
## Pros and Cons of the Options
```

## Naming + numbering

- Files are numbered four-digit-zero-padded, in chronological order: `0001-`, `0002-`, etc.
- Slug after the number is kebab-case and short: `0004-cpp23-baseline.md`, not `0004-decided-to-bump-the-c-plus-plus-standard.md`.
- Numbers are never reused. If an ADR is superseded, mark the old one `Superseded by 000X` and write a new ADR.

## Statuses

- **Proposed** — drafted but not adopted. The decision has not been made; the ADR is the proposal.
- **Accepted** — decision made; the codebase reflects (or will reflect) it.
- **Deprecated** — decision is no longer in force but no replacement exists.
- **Superseded by 000X** — replaced by a newer ADR. Keep the old file in place; readers should be able to reach the newer one by following the link.

Never delete an ADR. The historical record matters more than tidiness.

## When to write an ADR

Write one when a decision:

1. Is hard to reverse (e.g. public API shape, license, parser choice).
2. Closes off other reasonable options the next contributor would otherwise re-litigate.
3. Encodes a project policy (code standards, contribution model, distribution surface).

Routine code review decisions, file-layout tweaks inside a module, and individual rule additions do not need ADRs. The roadmap covers those.

## Contribution

1. Pick the next free number.
2. Copy an existing ADR as a template — `0004-cpp23-baseline.md` is a good full example, `0003-module-decomposition.md` is a good example of a `Proposed`-status ADR.
3. Open a PR. The PR description is the place for back-and-forth; the ADR text should reflect the conclusion, not the debate.
4. Once merged with `Accepted` status, treat the ADR as load-bearing — changes to the decision require a new ADR (not an edit to the old one) unless you are correcting a typo.

## Index

| # | Status | Title |
| --- | --- | --- |
| [0001](0001-compiler-choice-slang.md) | Accepted | Compiler choice — Slang |
| [0002](0002-parser-tree-sitter-hlsl.md) | Accepted | Parser — tree-sitter-hlsl |
| [0003](0003-module-decomposition.md) | Proposed | Module decomposition (Phase 1+) |
| [0004](0004-cpp23-baseline.md) | Accepted | C++23 baseline + selective C++26 |
| [0005](0005-cicd-pipeline.md) | Accepted | CI/CD pipeline |
| [0006](0006-license-apache-2-0.md) | Accepted | License — Apache-2.0 |
| [0007](0007-rule-pack-expansion.md) | Accepted | Rule-pack expansion (+41 rules) |

The `_research/` subdirectory preserves the verbatim agent-research outputs that fed each ADR; treat those as the audit trail, not the canonical decision.
