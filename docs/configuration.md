# Configuration

> **Status:** pre-v0 — the config schema is at design stage and may change before v0.1. See [ROADMAP](../ROADMAP.md).

`hlsl-clippy` reads `.hlsl-clippy.toml` from the invocation directory or the nearest ancestor directory that contains one. When no config file is found, all rules run at their default severity.

## `[rules]` — per-rule severity

The `[rules]` table maps rule IDs to severity levels:

```toml
[rules]
pow-to-mul              = "warn"
redundant-saturate      = "deny"
derivative-in-divergent-cf = "deny"
```

Omitting a rule from `[rules]` leaves it at its built-in default (shown in the [rules catalog](rules/index.md)).

## Severity levels

| Level  | Behaviour                                        |
|--------|--------------------------------------------------|
| `allow` | Rule is silenced; no diagnostic emitted.        |
| `warn`  | Diagnostic emitted; exit code 1.                |
| `deny`  | Diagnostic emitted; exit code 2 (hard error).   |

These mirror Rust's `clippy::` lint level names intentionally — the semantics are the same.

## `[includes]` and `[excludes]` — glob arrays

Control which files are linted:

```toml
[includes]
paths = ["shaders/**/*.hlsl", "src/gpu/**/*.hlsl"]

[excludes]
paths = ["shaders/vendor/**", "shaders/generated/**"]
```

When `[includes]` is absent, all `.hlsl` files under the invocation directory are linted (subject to `[excludes]`).

## `[[overrides]]` — per-directory rule tuning

The `[[overrides]]` array allows different severity levels for specific directory subtrees:

```toml
[[overrides]]
paths = ["shaders/experimental/**"]
rules = { derivative-in-divergent-cf = "warn" }

[[overrides]]
paths = ["shaders/vendor/**"]
rules = { pow-to-mul = "allow", redundant-saturate = "allow" }
```

Overrides are applied in order; later entries win.

## Inline suppression

### Line scope

Suppress a single rule on the next line (or on the same line, depending on final syntax — TBD):

```hlsl
// hlsl-clippy: allow(pow-to-mul)
float k = pow(x, 2.0);
```

### File scope

Placing a file-scope suppression at the very top of the file (before any non-comment token) silences the named rule for the entire file:

```hlsl
// hlsl-clippy: allow(*)
// Suppress all diagnostics — this is a vendor file we do not control.
```

Using `allow(*)` suppresses all rules. Individual rules can also be listed: `allow(pow-to-mul, redundant-saturate)`.
