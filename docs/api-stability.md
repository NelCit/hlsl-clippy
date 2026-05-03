# API stability commitment

> **Status (2026-05-02):** prospective. The commitment in this document
> applies *from v1.0 onwards*. Pre-v1.0 (v0.x) releases reserve the right
> to break any of the surfaces described below; the doc exists now so
> that the v1.0 freeze is explicit, reviewable, and mechanically
> enforceable. See [ADR 0018 §5 criterion #1](decisions/0018-v08-research-direction.md).

`shader-clippy` v1.0 freezes a curated set of source-level, binary, and
wire-protocol surfaces. A v1.0 → v1.x bump may not change the *binary
shape* of any item in the "Stable surface" table below. Removing or
renaming a public type is a v2.0 break. New rules, new optional fields,
new config keys, and new diagnostic codes are *additive* and remain
v1.x-compatible.

## Stable surface

The following surfaces are covered by the v1.0 stability commitment.

### C++ public-header surface (`core/include/shader_clippy/`)

Every type and function declared under `core/include/shader_clippy/` is
part of the stable surface. Adding a new field to an aggregate at the
end of the layout is permitted; reordering or removing an existing
field is a v2.0 break.

| Header | Stable items |
|---|---|
| `diagnostic.hpp` | `Diagnostic`, `Severity`, `TextEdit`, `Fix` |
| `source.hpp` | `SourceId`, `ByteSpan`, `Span`, `SourceLocation`, `SourceFile`, `SourceManager` |
| `rule.hpp` | `Rule`, `RuleContext`, `Stage`, `ExperimentalTarget`, forward decls (`AstCursor`, `AstTree`) |
| `lint.hpp` | `LintOptions`, all four `lint(...)` overloads, `make_default_rules()` |
| `config.hpp` | `Config`, `RuleSeverity` |
| `rewriter.hpp` | `Rewriter` |
| `suppress.hpp` | `SuppressionSet` |
| `reflection.hpp` | `ReflectionInfo` and its public sub-aggregates |
| `control_flow.hpp` | `ControlFlowInfo` and its public sub-aggregates |
| `ir.hpp` | `IrInfo` and its public sub-aggregates |
| `version.hpp` | `version()` |

### CLI surface

The `shader-clippy` executable contract is stable for the following
flags. Their stdout/stderr formats are part of the contract:

| Flag | Contract |
|---|---|
| `--version` | Prints the semver string on stdout, exits 0. No prefix, no trailing whitespace beyond a newline. |
| `--help` | Prints usage to stdout, exits 0. Layout may add new sections; existing sections may not be removed. |
| `--format=human\|json\|github-annotations` | Selects diagnostic rendering. JSON schema is additive-only across v1.x. |
| `--fix` | Applies machine-applicable rewrites in place. Idempotent. |
| `--config <path>` | Overrides `.shader-clippy.toml` lookup. |
| `--target-profile <p>` | Overrides the per-stage Slang reflection profile. |

Exit codes — `0` success, `1` lint findings emitted, `2` invocation
error — are stable. The default `--format` (auto-detection of
`GITHUB_ACTIONS=true`) is stable.

### LSP wire-protocol surface (`shader-clippy-lsp`)

The LSP server speaks standard LSP over stdio. The server-specific
extensions covered by the stability commitment:

- The set of `clippy::*` engine-diagnostic codes:
  - `clippy::reflection`
  - `clippy::cfg-skip`
  - `clippy::cfg`
  - `clippy::ir-not-implemented`
  - `clippy::ir-compiled-out`
  - `clippy::ir-partial`
  - `clippy::ir`
  - `clippy::malformed-suppression`
  - `clippy::config`
  - `clippy::query-compile`

  New `clippy::*` codes may be added in v1.x; existing codes may not be
  renamed or repurposed.

- The standard LSP capability set advertised by the server in
  `initialize` is additive-only across v1.x.

- `quickfix` code-actions surfaced for diagnostics with attached
  `Fix`es preserve their ordering relative to the diagnostic's
  `fixes` vector.

## Explicitly NON-stable surface

The items below are not covered by the stability commitment and may
change without a major-version bump.

| Surface | Reason |
|---|---|
| `core/src/**/*.hpp` private headers | Implementation detail. Tests may include these via the `core/src` include path; downstream consumers must not. |
| `core/src/parser_internal.hpp` | tree-sitter `TSNode` wrapper details. The forward-declared `AstCursor` / `AstTree` in `rule.hpp` are stable, the underlying definitions are not. |
| `tools/dev-shell.ps1`, `tools/fetch-slang.{ps1,sh}`, `tools/release-audit.{ps1,sh}` | Developer-facing scripts, version-tied to internal layout. |
| `tools/refactor-ast-helpers.ps1`, `tools/refresh-rule-doc-*.ps1` | One-off / maintenance scripts. May be removed without notice. |
| Undocumented `.shader-clippy.toml` keys | Anything not listed in `docs/configuration.md`. Recognised undocumented keys may be promoted to documented keys (additive) or silently dropped. |
| Exact wording of diagnostic *messages* | Only the diagnostic *code* (the rule id, e.g. `pow-const-squared`) is stable. The human-readable message text may be tightened, retranslated, or rephrased in any release. |
| Performance characteristics | We aim to not regress lint time or memory; we do not make that a contract. |
| `[experimental]` config keys | Anything under `[experimental]` (including the IHV target gates `rdna4` / `blackwell` / `xe2`) is v1.x-mutable. |
| Internal CI / lint workflow shape | `.github/workflows/*.yml` may be restructured at any time. |
| Docs site URLs other than the rule-id permalinks | The shape of `docs/decisions/*.md`, `docs/blog/*.md` paths is stable; everything else is not. |
| Test fixtures under `tests/fixtures/`, golden snapshots under `tests/golden/`, the corpus under `tests/corpus/` | Test-only artefacts. May be reshuffled. |

## What to do if you find a regression

If a v1.x release breaks a surface listed in the "Stable surface" table
above, file a bug at
[github.com/NelCit/shader-clippy/issues](https://github.com/NelCit/shader-clippy/issues)
with the `regression` label. Include the previous and current version
strings, a minimal repro (one .hlsl file is ideal), and the diff in
behaviour. Stability bugs are treated as release blockers — the next
v1.x patch will revert or compatibility-shim the offending change.
For non-stable surfaces, please still file the report as a regular
bug; we'll fix what we can and document the breakage in the
`CHANGELOG.md` for the affected release.
