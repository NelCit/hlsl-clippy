# CI Integration

> **Status:** pre-v0 — CI gate mode lands in Phase 6. See [ROADMAP](../ROADMAP.md). The YAML and schema below are design-stage sketches; exact flags and field names may change before the v0.5 release.

## GitHub Actions example

```yaml
# .github/workflows/hlsl-lint.yml  (pre-v0 design — not functional yet)
name: HLSL lint

on: [push, pull_request]

jobs:
  hlsl-clippy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Download hlsl-clippy
        run: |
          curl -sSfL https://github.com/NelCit/hlsl-clippy/releases/latest/download/hlsl-clippy-linux-x86_64.tar.gz | tar xz
      - name: Lint shaders
        run: ./hlsl-clippy lint --format=github shaders/
```

`--format=github` emits GitHub Actions workflow command annotations (`::warning file=...` / `::error file=...`) so diagnostics appear inline in the pull request diff view.

## Exit codes

| Code | Meaning |
|------|---------|
| `0`  | No diagnostics at or above `warn` severity. |
| `1`  | One or more `warn`-level diagnostics; no `deny`-level diagnostics. |
| `2`  | One or more `deny`-level (error) diagnostics. |
| `3`  | Configuration error (malformed `.hlsl-clippy.toml`, unknown rule ID, etc.). |
| `4`  | Internal error (parse failure, Slang crash, unexpected exception). |

Exit code `2` is intended to fail CI unconditionally. Exit code `1` can be tolerated on feature branches and treated as hard-fail only on `main`, depending on repo policy.

## JSON output schema

Pass `--format=json` to get a JSON array on stdout. Each element is a diagnostic object:

```json
[
  {
    "code":     "pow-to-mul",
    "severity": "warn",
    "file":     "shaders/pbr.hlsl",
    "span":     [412, 425],
    "message":  "pow(x, 2.0) can be simplified to x*x",
    "fixes": [
      {
        "applicability": "machine-applicable",
        "edits": [
          { "start": 412, "end": 425, "replacement": "x * x" }
        ]
      }
    ]
  }
]
```

Field notes:

- `span` — `[start, end)` byte offsets into the file (UTF-8, zero-indexed). Convert to line/column via the `SourceManager` tables or an external UTF-8 offset tool.
- `severity` — one of `"info"`, `"warn"`, `"error"`.
- `fixes` — empty array when `applicability` is `none`. Multiple edits in one fix are applied atomically.

The JSON schema will be versioned and stabilised at v0.5. Until then, treat the field names as provisional.
