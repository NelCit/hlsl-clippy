import { defineConfig } from 'vitepress'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: 'hlsl-clippy',
  description: 'An HLSL linter for patterns dxc misses.',

  // GitHub Pages deployment: set base to your repo name.
  // base: '/hlsl-clippy/',  // uncomment when deploying to GitHub Pages

  themeConfig: {
    nav: [
      { text: 'Home', link: '/' },
      { text: 'Rules', link: '/rules/pow-const-squared' },
      { text: 'Architecture', link: '/architecture' },
      { text: 'Blog', link: '/blog/' },
    ],

    sidebar: {
      '/rules/': [
        {
          text: 'Rules',
          items: [
            { text: 'pow-const-squared', link: '/rules/pow-const-squared' },
          ],
        },
      ],

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
            { text: 'Architecture', link: '/architecture' },
          ],
        },
      ],
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/NelCit/hlsl-clippy' },
    ],

    footer: {
      message:
        'Code: Apache-2.0. Blog prose: CC-BY-4.0.',
      copyright: '© 2026 NelCit',
    },

    editLink: {
      pattern:
        'https://github.com/NelCit/hlsl-clippy/edit/main/docs/:path',
      text: 'Edit this page on GitHub',
    },
  },
})
