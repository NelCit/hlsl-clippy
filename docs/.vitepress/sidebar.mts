// Sidebar autogeneration for /rules/.
//
// Globs every `docs/rules/*.md` (excluding `_template.md` and `index.md`),
// parses the YAML front-matter for `id` / `category` / `phase` / `severity` /
// `applicability`, and groups entries into VitePress sidebar buckets.
//
// Front-matter parsing is intentionally hand-rolled (a tiny, top-of-file YAML
// reader) so we don't have to vendor `gray-matter` for what is a strictly
// regular subset of YAML used in this repo. If a future rule page needs nested
// or list-typed front-matter, swap in `gray-matter` here.

import { readFileSync, readdirSync } from 'node:fs'
import { join } from 'node:path'

export interface SidebarItem {
  text: string
  link?: string
  items?: SidebarItem[]
  collapsed?: boolean
}

interface RuleFrontMatter {
  id: string
  category: string
  phase?: number | string
  severity?: string
  applicability?: string
  slug: string // basename without `.md`
}

// Canonical category ordering for the sidebar. Keep this list in sync with the
// `category:` values used across `docs/rules/*.md`. Categories absent here are
// appended alphabetically under "Other" so a new rule with an unknown category
// still renders without a config edit.
const k_category_order: readonly string[] = [
  'math',
  'saturate-redundancy',
  'bindings',
  'texture',
  'workgroup',
  'control-flow',
  'mesh',
  'dxr',
  'work-graphs',
  'ser',
  'cooperative-vector',
  'long-vectors',
  'opacity-micromaps',
  'sampler-feedback',
  'vrs',
  'wave-helper-lane',
  'packed-math',
  'memory',
  'misc',
] as const

const k_category_titles: Record<string, string> = {
  math: 'Math',
  'saturate-redundancy': 'Saturate / redundancy',
  bindings: 'Bindings',
  texture: 'Texture / sampling',
  workgroup: 'Workgroup / groupshared',
  'control-flow': 'Control flow',
  mesh: 'Mesh / amplification',
  dxr: 'Ray tracing (DXR)',
  'work-graphs': 'Work graphs',
  ser: 'Shader execution reordering (SER)',
  'cooperative-vector': 'Cooperative vectors',
  'long-vectors': 'Long vectors',
  'opacity-micromaps': 'Opacity micromaps',
  'sampler-feedback': 'Sampler feedback',
  vrs: 'Variable-rate shading',
  'wave-helper-lane': 'Wave / helper lane',
  'packed-math': 'Packed math',
  memory: 'Memory / IR-level',
  misc: 'Numerical / misc',
}

// Match the first YAML document at the very top of the file:
//   ---
//   key: value
//   key: value
//   ---
const k_front_matter_re = /^---\r?\n([\s\S]*?)\r?\n---/

// Strip surrounding quotes if present (`"foo"` / `'foo'` → `foo`).
function unquote(value: string): string {
  const trimmed = value.trim()
  if (
    (trimmed.startsWith('"') && trimmed.endsWith('"')) ||
    (trimmed.startsWith("'") && trimmed.endsWith("'"))
  ) {
    return trimmed.slice(1, -1)
  }
  return trimmed
}

function parse_front_matter(text: string): Record<string, string> | null {
  const match = text.match(k_front_matter_re)
  if (!match) return null
  const out: Record<string, string> = {}
  for (const raw_line of match[1].split(/\r?\n/)) {
    const line = raw_line.trim()
    if (line === '' || line.startsWith('#')) continue
    const colon = line.indexOf(':')
    if (colon === -1) continue
    const key = line.slice(0, colon).trim()
    const value = unquote(line.slice(colon + 1))
    out[key] = value
  }
  return out
}

function load_rules(rules_dir: string): RuleFrontMatter[] {
  const entries = readdirSync(rules_dir)
  const rules: RuleFrontMatter[] = []
  for (const file of entries) {
    if (!file.endsWith('.md')) continue
    if (file === '_template.md' || file === 'index.md') continue
    const slug = file.slice(0, -'.md'.length)
    const raw = readFileSync(join(rules_dir, file), 'utf8')
    const fm = parse_front_matter(raw)
    if (!fm || !fm.id || !fm.category) {
      // Skip pages that are missing the load-bearing front-matter rather than
      // crashing the build; orchestrator-side audit catches them via the
      // separate flagging step in this task's report.
      continue
    }
    rules.push({
      id: fm.id,
      category: fm.category,
      phase: fm.phase,
      severity: fm.severity,
      applicability: fm.applicability,
      slug,
    })
  }
  rules.sort((a, b) => a.id.localeCompare(b.id))
  return rules
}

function category_rank(category: string): number {
  const idx = k_category_order.indexOf(category)
  return idx === -1 ? k_category_order.length : idx
}

function category_title(category: string): string {
  return k_category_titles[category] ?? category
}

export function build_rules_sidebar(rules_dir: string): SidebarItem[] {
  const rules = load_rules(rules_dir)
  const by_category = new Map<string, RuleFrontMatter[]>()
  for (const r of rules) {
    const bucket = by_category.get(r.category) ?? []
    bucket.push(r)
    by_category.set(r.category, bucket)
  }

  const categories = Array.from(by_category.keys()).sort((a, b) => {
    const ra = category_rank(a)
    const rb = category_rank(b)
    if (ra !== rb) return ra - rb
    return a.localeCompare(b)
  })

  const sections: SidebarItem[] = []
  sections.push({
    text: 'Overview',
    items: [{ text: 'All rules', link: '/rules/' }],
  })

  for (const cat of categories) {
    const bucket = by_category.get(cat) ?? []
    sections.push({
      text: category_title(cat),
      collapsed: true,
      items: bucket.map((r) => ({
        text: r.id,
        link: `/rules/${r.slug}`,
      })),
    })
  }

  return sections
}

// Exposed for the data-loader / index page that wants the raw list.
export function load_rules_for_index(rules_dir: string): RuleFrontMatter[] {
  return load_rules(rules_dir)
}

export const k_categories_in_order = k_category_order
export const k_category_display_titles = k_category_titles
