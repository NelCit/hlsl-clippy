# tools/

Maintainer scripts. Each entry below names the script, summarises what
it does, and links to the canonical documentation (CLAUDE.md, an ADR,
or the script's own header comment) where the rationale lives.

## Build + dev environment

- **`dev-shell.{ps1,sh}`** — enter a shell with VS Build Tools (Windows)
  / Homebrew LLVM (macOS) / apt LLVM (Linux) on PATH plus the Slang
  prebuilt cache wired up so `cmake` / `ninja` / `ctest` resolve. Idempotent
  via `HLSL_CLIPPY_DEV_SHELL_READY`. See CLAUDE.md §"Build from source".
- **`fetch-slang.{ps1,sh}`** — populate the per-user Slang prebuilt cache
  at `%LOCALAPPDATA%/hlsl-clippy/slang/<version>/` (Windows) or
  `$HOME/.cache/hlsl-clippy/slang/<version>/` (Linux + macOS). Pinned by
  `cmake/SlangVersion.cmake`. See CLAUDE.md §"Slang prebuilt cache".
- **`install-hooks.{ps1,sh}`** + `git-hooks/` — install the project
  pre-commit clang-format gate. See `tools/git-hooks/README.md`.

## Smoke + bench utilities

- **`slang-smoke/`** + **`treesitter-smoke/`** — minimal CLIs that
  exercise the vendored Slang and tree-sitter-hlsl APIs.
- **`smoke-lsp.js`** + **`smoke-vsix.js`** — Node smoke tests for the
  LSP server and the packaged VS Code extension (driven by
  `vscode-extension/`).
- **`bench-diff.py`** — diff bench harness output between two runs;
  fed by `.github/workflows/bench.yml`.

## Goldens

- **`update-goldens.{ps1,sh}`** — refresh `tests/golden/*.json` after
  intentional output-format changes. Manual (not part of CI).

## Release pipeline

- **`build-vsix-local.{ps1,sh}`** — build the platform-specific `.vsix`
  for VS Code Marketplace publication; mirrors what
  `.github/workflows/release-vscode.yml` does on tag.
- **`release-audit.{ps1,sh}`** — pre-tag readiness audit (DCO, conventional
  commits, CHANGELOG entry, version-string sync, ADR index, public-header
  guard). Required before `git tag vX.Y.Z`. See ADR 0018 §5 #12.
- **`release-checklist.md`** — canonical step list for a release. Bumps
  `core/src/version.cpp`, `vscode-extension/package.json`, CHANGELOG.

## Doc maintenance

- **`refresh-rule-doc-status.{ps1,sh}`** + **`refresh-rule-doc-status-v2.ps1`**
  + **`refresh-rule-doc-blog-link.ps1`** + **`refresh-since-version.ps1`** —
  bulk-update front-matter + status banners across `docs/rules/*.md`.
  Idempotent.
- **`refactor-ast-helpers.ps1`** — rename / re-route AST-helper imports
  across `core/src/rules/*.cpp` after Phase 6+ helper-API changes.

## v1.0 / v1.1 readiness

- **`fp-rate-baseline.ps1`** — runs the CLI across `tests/corpus/`, aggregates
  per-rule firing counts, regenerates `tests/corpus/FP_RATES.md`. The
  `Triage` column is initialised to `TODO`. ADR 0018 §5 #3.
- **`fp-rate-triage.ps1`** *(v1.1)* — applies a deterministic decision
  procedure (per-rule natural-domain mapping + heuristic-overfire list)
  to replace `TODO` rows in `tests/corpus/FP_RATES.md` with TP / FP /
  MIXED / NEEDS-HUMAN classifications. **Preserves maintainer-edited
  rows** across re-runs (only `TODO` rows get rewritten). Adds a
  "Above-budget rules (FP rate > 5%)" section at the top. ADR 0019
  §"v1.x patch trajectory" v1.1 ship list.
- **`adoption-poll.{ps1,sh}`** *(v1.1)* — captures Marketplace install
  count + GitHub-code-search downstream-integration count, appends one
  dated row to `docs/adoption-metrics.md`. Drives ADR 0018 §5 criteria
  #7 + #8 review for v1.1.x. **Cadence: monthly** — run by hand or
  schedule via `cron` / Task Scheduler. The script does NOT validate
  the install / repo thresholds — the maintainer reviews the trend
  before each release. Requires `vsce` (`npm i -g @vscode/vsce`) and
  `gh` (with `gh auth login`); the bash variant additionally needs
  `jq`. Missing tools degrade gracefully — sentinel `?` values land
  in the row instead of crashing.
