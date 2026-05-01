// VitePress data loader: emits the catalog used by `docs/rules/index.md`.
//
// VitePress picks this file up because of the `.data.ts` suffix and exposes
// the default-exported `data` to any page that imports it. The file globbed
// against is `docs/rules/*.md`; if you add or rename rule pages, the dev
// server reloads automatically thanks to the `watch` field below.

import { fileURLToPath, URL } from 'node:url'
import { defineLoader } from 'vitepress'
import {
  load_rules_for_index,
  k_categories_in_order,
  k_category_display_titles,
} from './sidebar'

export interface RuleEntry {
  id: string
  slug: string
  category: string
  phase: string
  severity: string
  applicability: string
}

export interface CategoryGroup {
  key: string
  title: string
  rules: RuleEntry[]
}

export interface RulesCatalog {
  total: number
  categories: CategoryGroup[]
}

declare const data: RulesCatalog
export { data }

const rules_dir = fileURLToPath(new URL('../rules', import.meta.url))

export default defineLoader({
  watch: ['../rules/*.md'],
  load(): RulesCatalog {
    const rules = load_rules_for_index(rules_dir)

    const by_category = new Map<string, RuleEntry[]>()
    for (const r of rules) {
      const entry: RuleEntry = {
        id: r.id,
        slug: r.slug,
        category: r.category,
        phase: r.phase ? String(r.phase) : '—',
        severity: r.severity ?? '—',
        applicability: r.applicability ?? '—',
      }
      const bucket = by_category.get(entry.category) ?? []
      bucket.push(entry)
      by_category.set(entry.category, bucket)
    }

    const ordered_keys = Array.from(by_category.keys()).sort((a, b) => {
      const ra = k_categories_in_order.indexOf(a)
      const rb = k_categories_in_order.indexOf(b)
      const na = ra === -1 ? k_categories_in_order.length : ra
      const nb = rb === -1 ? k_categories_in_order.length : rb
      if (na !== nb) return na - nb
      return a.localeCompare(b)
    })

    const categories: CategoryGroup[] = ordered_keys.map((key) => ({
      key,
      title: k_category_display_titles[key] ?? key,
      rules: (by_category.get(key) ?? []).sort((a, b) =>
        a.id.localeCompare(b.id),
      ),
    }))

    return {
      total: rules.length,
      categories,
    }
  },
})
