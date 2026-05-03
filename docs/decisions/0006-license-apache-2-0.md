---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [license, governance, phase-0]
---

# License — Apache-2.0 (code) + CC-BY-4.0 (docs) + DCO

## Context and Problem Statement

The repository currently ships an MIT `LICENSE` file. Before any third-party contribution lands and before any binary is uploaded to a release page, we need a deliberate license choice that:

- Is compatible with our vendored dependencies (Slang under Apache-2.0 + LLVM exception, tree-sitter under MIT, tree-sitter-hlsl under MIT, toml++ under MIT, nlohmann/json under MIT).
- Carries a patent grant — `shader-clippy` operates in GPU-compilation territory where multiple IHVs hold patents.
- Reads as a credible, non-amateur choice to AAA studios, IHVs, and engine teams who might want to integrate the linter into their CI.
- Makes contribution friction-free for a solo project (no CLA paperwork dance).
- Has a sensible answer for the prose side — the rule-catalog pages, the "why this matters on a GPU" blog series, are the reputation engine and want maximum reach with attribution preserved.

## Decision Drivers

- **Compatibility with Slang.** Slang is Apache-2.0 + LLVM exception; matching the upstream license eliminates one whole class of license-compatibility friction.
- **Patent grant + retaliation clause.** Apache-2.0 includes both. MIT does not.
- **AAA-studio / IHV adoption signal.** Apache-2.0 is the "we read CONTRIBUTING.md at three large projects" choice. MIT is fine but slightly weaker; AGPL is a negative credential for graphics-tools work.
- **Solo-maintainer ergonomics.** CLAs introduce friction and read as "preparing to monetize and lock the door." DCO has the same legal end-state and is what the Linux kernel, GitLab, and CNCF use.
- **Documentation reach.** Technical writing wants CC-BY-4.0 (Khronos, Mozilla MDN, academic preprints) — not CC-BY-SA which blocks commercial reuse.

## Considered Options

For the **code**:

1. MIT (current).
2. **Apache-2.0** (chosen).
3. BSD-3-Clause.
4. Dual MIT/Apache (Rust convention).
5. MPL-2.0 / EUPL.
6. AGPL-3.0.

For **contributions**:

1. **DCO** (chosen).
2. CLA (Individual + Corporate).

For **documentation / blog / rule-catalog pages**:

1. **CC-BY-4.0** (chosen).
2. CC-BY-SA-4.0.
3. Project license (Apache-2.0).
4. CC0 / public domain.

For **`tests/fixtures/`** (hand-written): project Apache-2.0.

For **`tests/corpus/`** (third-party shaders): each file retains its upstream license; `tests/corpus/SOURCES.md` tracks provenance + license per file. Apache/MIT/CC0 only; CC-BY allowed but never baked into the released binary unless attribution discipline is wired up.

## Decision Outcome

- **Code**: Apache-2.0. The `LICENSE` file currently still shows MIT — replacing it with the verbatim Apache-2.0 text is a separate task tracked in the ROADMAP "Licensing" section.
- **Documentation, blog posts, rule-catalog pages**: CC-BY-4.0. Footer "© 2026 NelCit, CC-BY-4.0." Code snippets inside docs stay under project Apache-2.0.
- **`tests/fixtures/`**: project Apache-2.0.
- **`tests/corpus/`**: per-file upstream license; `SOURCES.md` registry.
- **Contributions**: DCO. Signed-off-by on every commit, enforced via the DCO GitHub App or a small workflow (~10 lines).
- **Required files at repo root**:
  - `LICENSE` — verbatim Apache-2.0 text, unmodified.
  - `NOTICE` — short attribution paragraph + per-vendored-dep one-liners (Slang Apache-2.0 + LLVM exception; tree-sitter MIT; tree-sitter-hlsl MIT; toml++ MIT; nlohmann/json MIT; vscode-languageclient MIT).
  - `THIRD_PARTY_LICENSES.md` — full text of each vendored dep license, sectioned. Ships inside binary releases.
- **Naming**: keep `shader-clippy`. Rust-clippy precedent (2014, no Microsoft action); HLSL is descriptive; `dxc`, `glslang`, `naga` already coexist. No trademark filing pre-v0.

### Consequences

Good:

- Slang + tree-sitter + tree-sitter-hlsl + toml++ + nlohmann/json are all permissive and compatible.
- Patent grant signals legal-team-friendly to AAA / IHV consumers.
- DCO is friction-free for contributors; no paperwork for the solo maintainer.
- CC-BY-4.0 docs maximize reach (translations, mirroring, reuse in courses) while preserving attribution.
- `tests/fixtures/` under project license means we can use them in published rule pages without per-fixture attribution overhead.

Bad:

- Apache-2.0 is more verbose than MIT — release tarballs are slightly larger.
- `NOTICE` and `THIRD_PARTY_LICENSES.md` must stay in sync with vendored deps; one extra release-engineering checkbox.
- Tree-sitter-hlsl license must be **verified** before vendoring. Most TS grammars are MIT but some are Apache or unlicensed.

### Confirmation

- `LICENSE` replaced with verbatim Apache-2.0 text.
- `NOTICE` created at repo root.
- `THIRD_PARTY_LICENSES.md` created at repo root.
- `CONTRIBUTING.md` includes a "Why this license?" blurb (paraphrase): _"shader-clippy is Apache-2.0 because Slang is Apache-2.0, because patent grants matter for tools in GPU-compilation territory, because Apache-2.0 is the friction-free choice for game-engine and IHV consumers. Contributions accepted under DCO — no CLA."_
- DCO check enforced on PRs (DCO GitHub App or workflow).
- README badges: `build` · `license: Apache-2.0` · `version` · `sponsors`. Skip "PRs welcome" badge.

## Pros and Cons of the Options

### Code — Apache-2.0 (chosen)

- Good: matches Slang upstream; compatible with all vendored deps.
- Good: explicit patent grant + retaliation clause.
- Good: positively signals to AAA / IHV legal teams.
- Bad: more verbose than MIT.

### Code — MIT

- Good: short, ubiquitous, MIT-compat-with-everything.
- Bad: no patent grant. Tools in GPU-compilation territory want one.
- Bad: weaker reputation signal than Apache-2.0 in graphics-tools-hire context.

### Code — BSD-3-Clause

- Like MIT. Same patent-grant gap. No reason to pick it over MIT.

### Code — Dual MIT/Apache

- Good: Rust convention; maximum downstream flexibility.
- Bad: signals "Rust person" more than "graphics person" — wrong reputation play for this project.
- Bad: extra paperwork; solo maintainer doesn't benefit from the flexibility.

### Code — MPL-2.0 / EUPL

- File-level / weak copyleft. Acceptable but unusual in graphics tooling. Adoption friction at studios with strict permissive-only policies.

### Code — AGPL-3.0

- Negative credential for graphics-tools work. Studios won't touch it. Explicitly **rejected**.

### Contributions — DCO (chosen)

- Good: zero-friction; Signed-off-by on every commit.
- Good: Linux kernel / GitLab / CNCF precedent.
- Bad: no centralized contributor record; CLA-style "we own the rights" guarantees absent.

### Contributions — CLA

- Good: explicit licensing assignment; useful if relicensing later.
- Bad: paperwork friction; reads as "preparing to monetize" — wrong signal for an open-source reputation play.

### Docs — CC-BY-4.0 (chosen)

- Good: standard for technical writing wanting maximum reach with attribution preserved (Mozilla MDN, Khronos, academic preprints).
- Good: commercial reuse allowed, attribution required.
- Bad: not the same license as the code; readers must understand the boundary.

### Docs — CC-BY-SA-4.0

- Bad: share-alike blocks commercial reuse. Wrong choice for a reputation play that wants the rule pages cited in studio-internal training materials.

### Docs — Apache-2.0 (project license)

- Good: one license everywhere.
- Bad: Apache-2.0 patent / notice clauses are awkward on prose; CC-BY-4.0 is the convention readers expect.

### Docs — CC0 / public domain

- Good: zero friction.
- Bad: no attribution requirement — undermines the reputation engine that's the point.

## Links

- Verbatim research: `_research/license-recommendation.md` §1, §3, §5, §6.
- Related: ADR 0001 (Slang license alignment), ROADMAP "Licensing" section.
