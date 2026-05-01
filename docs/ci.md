---
title: CI integration
outline: deep
---

# CI integration

`hlsl-clippy` is designed to drop into a CI pipeline as a gate. Two
output formats are tuned for CI:

- `--format=github-annotations` — emits GitHub Actions workflow
  command lines (`::warning file=...,line=...,col=...::msg`) so
  diagnostics show up inline on the pull request diff.
- `--format=json` — emits a flat JSON array on stdout for any CI
  provider that wants to parse and post-process. Stable schema since
  v0.5.

When `$GITHUB_ACTIONS=true` and `--format` is unset,
`github-annotations` is selected automatically.

## GitHub Actions example

A copy-paste-able starter workflow lives at
[`docs/ci/lint-hlsl-example.yml`](https://github.com/NelCit/hlsl-clippy/blob/main/docs/ci/lint-hlsl-example.yml).
Drop it at `.github/workflows/lint-hlsl.yml` in your repo and adjust
the path glob + version pin:

```yaml
name: HLSL lint

on:
  push:
    branches: ["**"]
    paths: ["**/*.hlsl", "**/*.hlsli", "**/.hlsl-clippy.toml"]
  pull_request:
    paths: ["**/*.hlsl", "**/*.hlsli", "**/.hlsl-clippy.toml"]

jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Download hlsl-clippy
        env:
          HLSL_CLIPPY_TAG: v0.5.3
        run: |
          set -euo pipefail
          asset="hlsl-clippy-${HLSL_CLIPPY_TAG#v}-linux-x86_64.tar.gz"
          curl -sSL -o /tmp/hlsl-clippy.tar.gz \
            "https://github.com/NelCit/hlsl-clippy/releases/download/${HLSL_CLIPPY_TAG}/${asset}"
          mkdir -p /tmp/hlsl-clippy
          tar -xzf /tmp/hlsl-clippy.tar.gz -C /tmp/hlsl-clippy --strip-components=1
      - name: Lint shaders
        run: |
          set -euo pipefail
          shopt -s globstar nullglob
          fail=0
          for shader in shaders/**/*.hlsl; do
            /tmp/hlsl-clippy/hlsl-clippy lint "$shader" \
              --format=github-annotations \
              || fail=$?
          done
          exit "$fail"
```

## Exit codes

| Code | Meaning |
|------|---------|
| `0`  | No diagnostics emitted. |
| `1`  | At least one `warning`-level (or `note`-level) diagnostic. |
| `2`  | At least one `error`-level diagnostic, OR the linter itself failed (file not found, malformed `.hlsl-clippy.toml`, etc.). |

Today exit code `2` is overloaded: a malformed config and a
rule-emitted error both surface as `2`. Distinguishing the two cases
requires inspecting stderr. A dedicated config-error exit code is on
the v0.6 backlog.

## Promoting warnings to errors

If your repo policy wants `pow-const-squared` to fail CI rather than
just warn, override its severity in `.hlsl-clippy.toml`:

```toml
[rules]
pow-const-squared = "error"
```

The CLI will then exit with `2` whenever the rule fires.

## JSON output schema

Pass `--format=json` for a single JSON array on stdout:

```json
[
  {
    "file":                   "shader.hlsl",
    "line":                   42,
    "column":                 14,
    "byte_offset":            1036,
    "byte_end":               1051,
    "severity":               "warning",
    "rule":                   "pow-const-squared",
    "message":                "pow(x, 2.0) is equivalent to x*x",
    "machine_applicable_fix": true
  }
]
```

Field notes:

- `line` / `column` — 1-based, UTF-8 codepoint indexed.
- `byte_offset` / `byte_end` — `[start, end)` byte offsets into
  the file.
- `severity` — one of `"error"`, `"warning"`, `"note"`.
- `rule` — rule slug as documented in the
  [rules catalog](/rules/).
- `machine_applicable_fix` — `true` when `--fix` would apply a
  rewrite for this diagnostic.

The schema is stable from v0.5 onwards; new fields may be added,
existing fields will not be renamed or repurposed without a major
version bump.

## Other CI providers

The `github-annotations` format is GitHub-specific. Other providers
(GitLab, Azure Pipelines, Buildkite, Jenkins, etc.) typically parse
log output line-by-line — `--format=json` works everywhere.

A SARIF output format is on the v0.6+ backlog for native integration
with security-scanning dashboards.
