import { defineConfig } from 'vitepress'
import { fileURLToPath, URL } from 'node:url'
import { build_rules_sidebar } from './sidebar.mts'

// Resolve `docs/rules/` relative to this config file so the sidebar generator
// works regardless of the cwd VitePress is invoked from.
const rules_dir = fileURLToPath(new URL('../rules', import.meta.url))

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: 'hlsl-clippy',
  description:
    'Linter for HLSL — performance + correctness rules beyond what dxc catches',

  // GitHub Pages project page lives at https://nelcit.github.io/hlsl-clippy/.
  // The trailing slash on `base` is required by VitePress for asset paths.
  base: '/hlsl-clippy/',

  cleanUrls: true,
  lastUpdated: true,

  themeConfig: {
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Rules', link: '/rules/' },
      { text: 'Blog', link: '/blog/' },
      { text: 'Roadmap', link: '/roadmap' },
      {
        text: 'GitHub',
        link: 'https://github.com/NelCit/hlsl-clippy',
      },
    ],

    sidebar: {
      '/rules/': build_rules_sidebar(rules_dir),

      '/blog/': [
        {
          text: 'Blog',
          items: [
            { text: 'Index', link: '/blog/' },
            {
              text: 'Posts',
              items: [
                {
                  text: 'pow(x, 2.0) is hiding a transcendental in your shader',
                  link: '/blog/pow-const-squared',
                },
              ],
            },
          ],
        },
      ],

      '/': [
        {
          text: 'Documentation',
          items: [
            { text: 'Getting started', link: '/getting-started' },
            { text: 'Configuration', link: '/configuration' },
            { text: 'Architecture', link: '/architecture' },
            { text: 'CI integration', link: '/ci' },
            { text: 'LSP / IDE', link: '/lsp' },
            { text: 'Contributing', link: '/contributing' },
          ],
        },
        {
          text: 'Project',
          items: [
            { text: "What's new", link: '/changelog' },
            { text: 'Roadmap', link: '/roadmap' },
          ],
        },
      ],
    },

    outline: [2, 3],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/NelCit/hlsl-clippy' },
    ],

    editLink: {
      pattern: 'https://github.com/NelCit/hlsl-clippy/edit/main/docs/:path',
      text: 'Edit this page on GitHub',
    },

    footer: {
      message: '© 2026 NelCit — Apache-2.0 (code), CC-BY-4.0 (docs).',
      copyright: '',
    },

    search: {
      provider: 'local',
    },
  },
})
