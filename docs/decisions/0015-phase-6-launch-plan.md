---
status: Accepted
date: 2026-05-01
deciders: NelCit
tags: [phase-6, launch, v0.5, ci-gate, docs-site, marketplace, blog, planning]
---

# Phase 6 launch plan — v0.5.0 release

## Context and Problem Statement

Phases 0 → 5 shipped the engine, the rule packs (154 rules), the LSP
server, the VS Code extension, and the macOS CI matrix. The release
infrastructure (`.github/workflows/release.yml`,
`.github/workflows/release-vscode.yml`, `tools/release-checklist.md`)
landed with sub-phase 5e. What remains before tagging `v0.5.0` is the
**launch** itself: a small batch of polish work that converts a
working linter into a public artifact a graphics programmer would
trust enough to drop into their build.

Per `ROADMAP.md` §"Phase 6 — Launch (v0.5)":

> - CI gate mode: exit codes, JSON output, GitHub Actions reporter
>   (annotation format)
> - Documentation site: one page per rule with *why it matters*,
>   before/after, generated DXIL diff where instructive
> - Rule-pack catalog: `math`, `bindings`, `texture`, `workgroup`,
>   `control-flow`, `vrs`, `sampler-feedback`, `mesh`, `dxr`,
>   `work-graphs` togglable in config
> - Launch posts: graphics-programming Discord, r/GraphicsProgramming,
>   Hacker News, Twitter
> - Aggregate the blog posts into a "Why your HLSL is slower than it
>   has to be" series

The engine bullets (CI gate / docs site / catalog) are partly already
landed and need finishing, not designing. The communication bullets
(launch posts, blog series) are planning items the maintainer drives
manually. This ADR's job is to **scope** what counts as
"v0.5.0-ready", **stage** the work into landable sub-phases, and
**capture** the load-bearing decisions so they don't get re-litigated
mid-launch.

This ADR is a **plan**, in the same shape as ADR 0008 / 0009 / 0012 /
0013 / 0014. **No code is written by this ADR.** Sub-phases below
specify which implementation PRs land in what order; the
implementation work itself happens after the ADR is moved to Accepted.

## Decision Drivers

- **Don't slip the launch on cosmetic work.** The engine ships, the
  rules fire, the docs are useful. Three TODO markers in the README
  are not a launch blocker; the v0.5 tag is. Polish in v0.6.

- **CI gate mode is a release blocker.** Every other discoverability
  channel (HN, r/GraphicsProgramming) eventually points back to the
  question "can I drop this in my CI?" — the answer must be a one-line
  copy-paste of the GitHub Actions step.

- **Per-rule blog post density at launch ≠ 154.** Aspirational target
  is one post per rule; realistic launch target is **the seven
  category overview posts plus the existing
  `pow-const-squared` deep-dive**. Each category overview links to its
  rule pages and explains the GPU mechanism shared across the category
  (e.g. "transcendentals on every lane" for math, "LDS bank conflicts"
  for workgroup). One post per rule is a long-tail follow-up across
  v0.6 / v0.7.

- **Marketplace listing lights up automatically when `VSCE_PAT`
  exists.** Per ADR 0014 §"Sub-phase 5e", `release-vscode.yml`
  gracefully skips publish when the secret is absent. Maintainer can
  cut the v0.5 tag without a Marketplace listing and add the listing
  on a v0.5.1 patch tag once the publisher PAT is provisioned. This
  decouples the engineering ship from the Marketplace bureaucracy.

- **Single-pass tag, no rebase-and-retag.** Per Conventional Commits +
  CLAUDE.md "Repo conventions": never force-push `main`. The release
  workflow assumes the tag is immutable. Pre-tag verification (the
  release-readiness audit pattern from ADR 0005) catches the surprises
  before the tag goes out.

- **Don't break existing CLI consumers.** No CLI flag rename, no JSON
  schema rename. The `--format=json` output landed in Phase 1 and any
  CI-gate work in 6a is additive (new flags, new exit-code policy
  knobs) — never breaking changes to the existing `--fix` /
  `--config` / `--target-profile` shape.

## Considered Options

### Option A — Ship a "v0.5-rc1" candidate first; tag v0.5.0 after a soak window

A prerelease tag (`v0.5.0-rc1`) cuts the same artifacts under the
`prerelease: true` flag in `release.yml`; consumers test for a week;
the final tag follows. Pros: rolls back any release-pipeline bug
without burning the v0.5.0 tag. Cons: the project has zero external
consumers at launch, so a soak window soaks against… nothing. The
release-readiness audit (`adb5548f56dabddcc`, 2026-05-01) is the soak
that catches the pipeline bugs. **Rejected** — adds calendar time
without buying signal.

### Option B — Tag directly from `main` once the audit returns green

`tools/release-checklist.md` step-by-step — bump versions, update
CHANGELOG, run a clean local build, push the tag, observe the
workflow, publish the draft Release. The audit-then-tag pattern was
already demonstrated by the 2026-05-01 audit (which surfaced the
`version.cpp 0.0.0` blocker, the missing CHANGELOG promotion, the
`npm ci` lockfile gap, and the floating `softprops/action-gh-release@v2`
SHA). All four landed in commit `25c2001`. **Accepted.**

### Option C — Defer the VS Code extension to v0.5.1

Tag a CLI-only v0.5.0; ship the extension on a follow-up tag once the
Marketplace publisher account is fully provisioned. Pros: smaller
launch surface, fewer simultaneous failure modes. Cons: the LSP /
extension shipped in Phase 5 sub-phase 5c — not shipping it at v0.5.0
defeats the point of Phase 5 landing into v0.5. The extension already
gates Marketplace publish on `VSCE_PAT` (graceful skip when absent),
so the secret-bureaucracy concern isn't a real reason to split the
tag. **Rejected.**

## Decision Outcome

**Chosen option:** Option B — tag `v0.5.0` directly from `main` once
the release-readiness audit returns green and the Phase 3 reflection
triage agent's fixes have landed, batched into the sub-phases below.

The launch is not "press the tag button." It is the seven sub-phases
below run in order, with two explicit gates:

- **Gate 1** (after sub-phase 6c): **engineering green-light** — full
  ctest passing modulo known pre-existing golden-snapshot flakes;
  docs site builds locally and on CI; release workflows pinned.
- **Gate 2** (after sub-phase 6e): **content green-light** — the
  seven category overview blog posts exist; the README + docs site
  cross-link is live (no `<!-- TODO -->` marker); `tools/release-checklist.md`
  has been re-walked end-to-end.

Only after both gates flip does sub-phase 6f cut the tag.

## Implementation sub-phases

Each sub-phase is the size of one PR (or one parallel batch of small
PRs). They are listed in dependency order. Sub-phase 6c can parallelize
across multiple writing agents the way Phase 2 / 3 / 4 rule packs did.

### 6a — CI gate-mode polish (1-2 days)

Single PR. Touches `cli/src/main.cpp`, `core/src/diagnostic.cpp`,
maybe `cli/src/main.cpp` again for argparse. Adds:

- **Exit code policy clarification.** Already documented in README
  ("`0` clean, `1` warnings, `2` errors or invocation failure"); make
  sure `cli/src/main.cpp` actually returns this consistently across
  the `--fix` path (currently the `--fix`-applied counts as "warnings"
  and exits 1 — confirm and document).
- **GitHub Actions annotation reporter.** New flag
  `--format=github-annotations` emitting
  `::warning file=...,line=...,col=...::message [rule-id]` per
  diagnostic. Auto-detected when `$GITHUB_ACTIONS=true`. The existing
  `--format=json` stays the long-term-stable surface; the
  annotation format is purely a presentation transform.
- **`.github/workflows/example/lint-hlsl.yml`** — a copy-paste-able
  starter step under `docs/ci/` that downloads the latest release,
  runs `hlsl-clippy lint --format=github-annotations` over the
  shader tree, and fails on exit 1 / 2.

Test surface: `tests/unit/test_cli_format_*.cpp` (one new file per
format flag value); the Catch2 driver invokes the CLI binary
out-of-process and asserts on stdout shape.

Deliverable: PR titled `feat(cli): GitHub Actions annotation reporter
+ CI gate-mode example workflow`.

### 6b — Docs site polish (1 day)

Single PR. Builds on the docs site already wired in commit `25c2001`:

- Replace the `<!-- TODO: enable docs site link once sub-phase 5e
  ships and the docs-site agent lands -->` marker in README.md with
  a live link to `https://nelcit.github.io/hlsl-clippy/`.
- Replace the `<!-- TODO: demo gif -->` marker with an `asciinema` /
  `vhs` recording of `hlsl-clippy lint --fix` against the corpus.
  Asset under `docs/public/demo.gif`; embedded inline in README.
- Pull the dead-link tolerations from `docs/.vitepress/config.mts`
  one at a time as the underlying gaps get fixed (or accept them as
  permanent for v0.5 and tighten in v0.6).
- Resolve the warning printed by `npm run docs:build`: chunk-size >
  500 KB. Manual chunk-splitting per VitePress doc.
- Add a `docs/changelog.md` page that loads `CHANGELOG.md` so the
  "What's new" sidebar entry resolves to a real page (currently
  dead).

Deliverable: PR titled `docs(site): launch-readiness polish — README
links, demo gif, dead-link cleanup`.

### 6c — Category overview blog posts (parallelizable; 3-5 days)

Eight PRs, runnable in parallel after 6a/6b land. One blog post per
rule **category** (math / bindings / texture / workgroup /
control-flow / mesh+dxr / work-graphs / wave+helper-lane) plus a
preface "Why your HLSL is slower than it has to be" landing post.
Each post is ~1500-2500 words, follows the existing
`docs/blog/pow-const-squared.md` structure (problem statement → GPU
mechanism → before/after → cycle counts where measurable → links
to the per-rule pages).

CC-BY-4.0 footer per ADR 0006 §"Documentation, blog posts".

Deliverable: 8 PRs titled `docs(blog): <category> overview` — each
agent picks a category, drafts the post against `docs/blog/`, links
into the category from `docs/.vitepress/config.mts` blog sidebar.

### 6d — Optional: Marketplace publisher provisioning (≤1 day, maintainer-driven)

Maintainer-only:

1. Create the `nelcit` publisher at https://aka.ms/vscode-create-publisher.
2. Generate a PAT scoped to `Marketplace → Manage`.
3. Add as `VSCE_PAT` repo secret.
4. Optional Open VSX twin: `OVSX_PAT`.

If skipped, the v0.5 tag still produces a `.vsix` artifact attached
to the GitHub Release; users sideload via
`code --install-extension hlsl-clippy-0.5.0.vsix`. Not a blocker.

### 6e — Final launch readiness audit (≤1 day)

One more pass of the release-readiness-audit pattern from ADR 0005,
running over the post-6c state. Goal: every blocker the
2026-05-01 audit raised has a verifiable fix; no new blockers
introduced by 6a-6c. Audit gates the tag.

If the audit returns RED, fix and re-audit; do not tag with known
RED items.

### 6f — Tag v0.5.0 (≤1 hour, maintainer-driven)

Maintainer:

1. Walk `tools/release-checklist.md` step by step (it already
   covers version bumps, but the bumps landed in `25c2001`; verify).
2. `git tag -s v0.5.0 -m "v0.5.0 — initial public release"`.
3. `git push origin v0.5.0`.
4. Watch `release.yml` and `release-vscode.yml` complete.
5. Edit the auto-drafted GitHub Release: add a one-paragraph
   highlight summary; flip from Draft → Published.

### 6g — Launch posts (maintainer-driven; calendar-day-0 + 1 + 2)

Posts queued for staggered publication:

- **Day 0 (tag day):** GitHub Release published; cross-post one-line
  announcement to graphics-programming Discord; tweet from
  `@NelCit`.
- **Day 1:** Hacker News submission. Title: "Show HN:
  hlsl-clippy — a Clippy for HLSL with 154 portable perf rules". Link
  to the GitHub Release. Open the comment thread before the post goes
  up so the first comment can pin the docs site link.
- **Day 2:** r/GraphicsProgramming long-form post referencing the
  category overview blog series. Pin the catalog page.

Stagger gives oncall time to triage the inevitable "this rule is wrong
on my Y2K-old shader" issue thread without all three platforms
firefighting at once.

## Risks & mitigations

- **Risk: Phase 3 reflection triage fixes don't land before launch.**
  The 13 reflection-stage failures the 2026-05-01 audit found will
  block the engineering green-light at Gate 1. Mitigation: triage
  agent dispatched in parallel with this ADR's drafting; if it
  doesn't return green, the affected rules get marked
  `severity = "off"` by default in the bundled
  `.hlsl-clippy.toml.example` and listed as "preview" in CHANGELOG —
  ship without them. v0.5.1 ramp.

- **Risk: Marketplace listing rejected on first submission.** Common
  causes: missing icon, missing categories field, wrong publisher
  identity. Mitigation: publisher provisioning (6d) happens BEFORE
  6f; first publish goes via `vsce package` locally + `vsce publish
  --dry-run` to catch validation errors before the workflow tries
  the real publish on tag.

- **Risk: Hacker News thread surfaces a critical bug.** Standard
  v0 launch hazard. Mitigation: have v0.5.1 patch-release dry-run
  in `tools/release-checklist.md` before tagging v0.5.0; first patch
  takes <1 hour of cycle time, not days.

- **Risk: macOS Marketplace .vsix rejected for unsigned LSP binary.**
  ADR 0014 §"Risks & mitigations" already flagged this. Mitigation:
  v0.5 ships unsigned macOS binary (Gatekeeper right-click-Open works);
  notarization is a v0.6 hardening track, gated on the
  `APPLE_NOTARY_KEY` secret which graceful-skips when absent.

- **Risk: docs site CI breaks under a fresh runner because the
  `.mts` rename + `decisions/**` srcExclude introduce subtle
  regressions.** Already verified locally on the maintainer's
  Windows machine via `npm run docs:build` (commit `25c2001`).
  Mitigation: docs.yml runs on every push to `main`; if it breaks
  post-tag, the docs site falls back to the previous successful
  deployment until a fix lands.

- **Risk: rule false positives on real-world shaders surface
  immediately.** The 27-shader corpus is permissively-licensed
  open-source; AAA studio codebases have idioms it doesn't capture.
  Mitigation: every false positive gets a `// hlsl-clippy: allow(rule-id)`
  escape hatch (Phase 1 suppression parser) AND a
  `severity = "off"` knob in `.hlsl-clippy.toml`. Document both
  prominently in the launch README.

## Consequences

### Positive

- v0.5 ships with a CI-gate story that doesn't require the user to
  wire up JSON parsing themselves.
- Launch posts have a coherent narrative anchor (the category
  overview blog series) instead of just "look at this README".
- Release pipeline is fully exercised before the tag — `25c2001`
  audit fixes proved the audit-then-fix loop works.

### Negative / accepted trade-offs

- 154 individual blog posts is a long-tail commitment. v0.5 ships
  with 8 (~5% coverage). Acceptable: the catalog page IS the
  comprehensive surface; per-rule deep-dives are a v0.6+ flywheel.
- Marketplace listing may go live on a v0.5.1 patch tag rather than
  v0.5.0 if PAT provisioning slips. Acceptable per Driver
  "Marketplace listing lights up automatically".

### Neutral

- This ADR does not specify the exact wording of the GitHub Release
  highlights paragraph. Maintainer drafts it from CHANGELOG.md
  `[0.5.0]` at tag-time. The `release.yml` workflow autogenerates
  the body from `git log` between tags as a fallback.

## Implementation status

- 6a — Pending. Issue tracking via the launch milestone.
- 6b — Pending. Issue tracking via the launch milestone.
- 6c — Pending. Eight parallel rule-pack-style writing agents.
- 6d — Maintainer-driven, may parallelise with 6c.
- 6e — Pending. Audit pattern from ADR 0005.
- 6f — Pending. Gates on 6a/6b/6c/6e green.
- 6g — Maintainer-driven, calendar-anchored on tag day.

## References

- `ROADMAP.md` §"Phase 6 — Launch (v0.5)"
- `tools/release-checklist.md`
- ADR 0005 — CI/CD pipeline (release artifact format, action SHA pin
  policy)
- ADR 0006 — License (Apache-2.0 for code; CC-BY-4.0 for blog/docs)
- ADR 0014 — Phase 5 LSP architecture (Marketplace publisher
  bureaucracy gated on `VSCE_PAT`)
- 2026-05-01 release-readiness audit
  (`adb5548f56dabddcc.output`) — RED verdict on
  `core/src/version.cpp` + 13 Phase 3 reflection failures + CHANGELOG
  promotion + Phase 4 absence; mechanical fixes shipped in commit
  `25c2001`.
