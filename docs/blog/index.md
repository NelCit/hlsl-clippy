---
title: "hlsl-clippy blog"
layout: doc
---

# hlsl-clippy blog

One post per rule. Each post explains the GPU mechanism behind a lint warning
in enough depth that you could derive the rule yourself.

The target reader is a graphics engineer who ships shaders to production and has
not profiled them in a while. Posts assume familiarity with HLSL and shader
stages. They do not assume familiarity with ISA internals — those are explained
from first principles.

## Posts

### v0.5.0 launch series — category overviews (2026-05-01)

The v0.5.0 launch ships eight category overviews plus a preface essay.
Each overview walks one rule-pack at GPU-mechanism level and links to
the per-rule pages for the deep dives.

| Date | Category | Title |
|------|----------|-------|
| 2026-05-01 | _preface_ | [Why your HLSL is slower than it has to be](./why-your-hlsl-is-slower-than-it-has-to-be) |
| 2026-05-01 | math | [Where the cycles go: math intrinsics on modern GPUs](./math-overview) |
| 2026-05-01 | workgroup | [Your groupshared array is bank-conflicting on RDNA](./workgroup-overview) |
| 2026-05-01 | control-flow | [Divergent control flow is the silent killer of your shader](./control-flow-overview) |
| 2026-05-01 | bindings | [Where root signatures and descriptor heaps quietly cost you](./bindings-overview) |
| 2026-05-01 | texture | [Texture sampling is doing more work than your shader admits](./texture-overview) |
| 2026-05-01 | mesh + dxr | [Mesh shaders + DXR](./mesh-dxr-overview) |
| 2026-05-01 | wave + helper-lane | [Wave intrinsics and helper-lane traps](./wave-helper-lane-overview) |
| 2026-05-01 | sm 6.9 | [SM 6.9: shader execution reordering, cooperative vectors, and the new ray-tracing primitives](./ser-coop-vector-overview) |

### Per-rule deep dives

| Date | Rule | Title |
|------|------|-------|
| 2026-04-30 | [`pow-const-squared`](./pow-const-squared) | [pow(x, 2.0) is hiding a transcendental in your shader](./pow-const-squared) |

---

## Conventions for contributors

### One post per rule

Every rule in `hlsl-clippy` gets exactly one blog post. The post lives at
`docs/blog/<rule-id>.md` and is linked from both the rule's doc page
(`docs/rules/<rule-id>.md`) and the table above.

### Front-matter

Every post must carry this front-matter block (fill in the fields):

```yaml
---
title: "<descriptive title — not just the rule name>"
date: YYYY-MM-DD
author: <GitHub handle>
rule-id: <rule-id>
tags: [hlsl, shaders, performance, ...]   # at minimum these four
license: CC-BY-4.0
---
```

### Pre-release callout

Until the rule ships in a tagged release, add this blockquote immediately after
the front-matter:

```markdown
> Companion post for the [<rule-id>](../../docs/rules/<rule-id>.md) rule.
> Project status: pre-v0; the rule lands as part of the v0.1 release.
```

Remove it (or update the version) once the rule is in a tagged release.

### Tone guide

Write graphics-engineer to graphics-engineer.

- **Explain the GPU mechanism, not the syntax.** The reader knows HLSL. They may
  not know the difference between a VALU op and an SFU op, or why issue width
  matters. That is what the post is for.
- **Be concrete and specific.** Name the architecture (RDNA2, Turing, Ada). Name
  the ISA instruction (`v_log2_f32`, not "the log instruction"). Cite cycle counts
  when you have a source; use a range when you don't.
- **Be honest about compiler behavior.** If the compiler often folds the pattern,
  say so and explain when it does and when it doesn't. The rule's value is in
  catching the cases it doesn't.
- **No emoji.** The tone is technical writing, not a Discord message.
- **Target length: 800-1500 words.** Long enough to be genuinely useful; short
  enough to read in one sitting.

### License

All blog posts are published under
[CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/). Include this footer
at the bottom of every post:

```markdown
*© YYYY NelCit, [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/).*
```

The code in the project itself is Apache-2.0 (see `LICENSE` at the repo root).
The blog prose is CC-BY-4.0.

### Adding a new post

1. Write `docs/blog/<rule-id>.md` following the conventions above.
2. Add a row to the table in `docs/blog/index.md`.
3. Add a "See also" link in `docs/rules/<rule-id>.md` pointing to the post.
4. Add the post to the `blog` sidebar in `docs/.vitepress/config.ts`.

### Local preview

From the repo root:

```bash
pnpm install
pnpm docs:dev
```

The dev server starts at `http://localhost:5173/`. Hot-reload is enabled for
Markdown changes.
