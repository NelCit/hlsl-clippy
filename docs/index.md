---
layout: home
hero:
  name: "hlsl-clippy"
  tagline: "Performance + correctness rules for HLSL — beyond what dxc catches"
  actions:
    - theme: brand
      text: Browse rules
      link: /rules/
    - theme: alt
      text: Read on GitHub
      link: https://github.com/NelCit/hlsl-clippy
features:
  - title: 154 rules across 5 phases
    details: AST-only math + redundancy, reflection-aware bindings, CFG-aware divergence, SM 6.7 / 6.8 / 6.9 surfaces.
  - title: Quick-fix in your editor
    details: VS Code extension + LSP server (sub-phase 5a-c). One-click rewrites where the fix is type-safe.
  - title: GPU reasoning per rule
    details: Every rule ships with a doc page explaining the hardware mechanism — RDNA / Turing / Ada / Xe-HPG.
---
