# Documentation

Reference documentation for `hlsl-clippy` — a linter for HLSL that catches performance and correctness issues beyond what `dxc` reports.

> **Status:** pre-v0 — documentation is being written ahead of feature shipping. See [ROADMAP](../ROADMAP.md).

## Contents

- [Getting Started](getting-started.md) — install, first lint invocation, basic config
- [Configuration](configuration.md) — `.hlsl-clippy.toml` schema, severity levels, inline suppression
- [Rules Catalog](rules/index.md) — all rules by category, severity, and applicability
- [Architecture](architecture.md) — pipeline overview for contributors
- [Contributing](contributing.md) — dev setup, build, rule authoring, DCO sign-off
- [LSP Integration](lsp.md) — language server (Phase 5, planned)
- [CI Integration](ci.md) — exit codes, JSON output, GitHub Actions (Phase 6, planned)

## Site generator

This docs tree is designed for **VitePress**.

Precedents in the linter and dev-tool ecosystem: rust-clippy's documentation site (mdBook) prioritises per-rule pages with minimal chrome, and Ruff's documentation (Sphinx → MkDocs Material) added live-search across hundreds of rules. VitePress occupies the same niche as MkDocs Material — Markdown-native, fast live-reload, built-in search — but its Vue-based theme is more composable and produces smaller bundles than Docusaurus, and its default theme ships a sidebar and code-block copy without configuration. For a project whose docs are mostly HLSL fenced blocks and ASCII pipeline diagrams, VitePress's zero-JS-framework dependency for readers, combined with sub-second cold builds, is the right balance between authoring simplicity and reader experience.
