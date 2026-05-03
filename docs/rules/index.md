---
title: Rules catalog
outline: [2, 2]
language_applicability: ["hlsl"]
---

<script setup>
import { withBase } from 'vitepress'
import { data as catalog } from '../.vitepress/rules-index.data.mts'
</script>

# Rules catalog

All rules grouped by category. Each entry links to the dedicated rule page,
which includes the GPU-mechanism reasoning and (where applicable) the
quick-fix rewrite.

> **Status:** v0.5.6 shipped — 154 rules ship end-to-end across the
> categories below. The catalog also lists ~30 doc-only stubs for
> rules queued in
> [ADR 0007](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0007-rule-pack-expansion.md) /
> [ADR 0010](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0010-sm69-rule-expansion.md) /
> [ADR 0011](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0011-candidate-rule-adoption.md)
> that have not landed yet — those pages document design intent
> ahead of code. See
> [ROADMAP](https://github.com/NelCit/shader-clippy/blob/main/ROADMAP.md)
> for the phase-by-phase plan.

**Total rules documented:** {{ catalog.total }}

## Legend

| Severity | Meaning                                                                  |
| -------- | ------------------------------------------------------------------------ |
| `error`  | Correctness issue or undefined behaviour. Blocks CI when mode is `deny`. |
| `warn`   | Performance anti-pattern or code smell. Default for most rules.          |
| `info`   | Style or modernisation hint. Never blocks CI.                            |

| Applicability        | Meaning                                                                |
| -------------------- | ---------------------------------------------------------------------- |
| `machine-applicable` | The fix can be applied automatically and is always correct.            |
| `suggestion`         | A fix is shown but requires human intent-verification before applying. |
| `none`               | No automated fix; the diagnostic only explains the problem.            |

<template v-for="group in catalog.categories" :key="group.key">

## {{ group.title }}

<table>
  <thead>
    <tr>
      <th>Rule</th>
      <th>Severity</th>
      <th>Applicability</th>
      <th>Phase</th>
    </tr>
  </thead>
  <tbody>
    <tr v-for="rule in group.rules" :key="rule.id">
      <td><a :href="withBase(`/rules/${rule.slug}`)"><code>{{ rule.id }}</code></a></td>
      <td><code>{{ rule.severity }}</code></td>
      <td><code>{{ rule.applicability }}</code></td>
      <td>{{ rule.phase }}</td>
    </tr>
  </tbody>
</table>

</template>

---

For contributors adding a new rule: start from
[`docs/rules/_template.md`](https://github.com/NelCit/shader-clippy/blob/main/docs/rules/_template.md). The catalog above is generated
from each page's YAML front-matter at build time — keep `id`, `category`,
`severity`, `applicability`, and `phase` accurate and the entry will appear
automatically.
