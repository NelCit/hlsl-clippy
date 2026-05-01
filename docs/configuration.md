---
title: Configuration
outline: deep
---

# Configuration

`hlsl-clippy` reads `.hlsl-clippy.toml` from the directory of each
linted file (or any ancestor directory up to the first `.git/`
parent). When no config file is found, all rules run at their
built-in default severity.

The CLI also accepts an explicit path:

```sh
hlsl-clippy lint --config path/to/.hlsl-clippy.toml shader.hlsl
```

## `[rules]` — per-rule severity

The `[rules]` table maps rule IDs to severity levels:

```toml
[rules]
pow-const-squared           = "warn"
redundant-saturate          = "warn"
derivative-in-divergent-cf  = "error"
clamp01-to-saturate         = "off"
```

Omitting a rule from `[rules]` leaves it at its built-in default
(shown in the [rules catalog](/rules/)).

## Severity levels

| Level     | Behaviour                                              |
|-----------|--------------------------------------------------------|
| `error`   | Diagnostic emitted; exit code `2` (hard error).        |
| `warning` | Diagnostic emitted; exit code `1`.                     |
| `note`    | Informational diagnostic; exit code `1`.               |
| `off`     | Rule is silenced; no diagnostic emitted.               |

The shorthand `"warn"` is accepted as an alias for `"warning"`.

## `includes` and `excludes` — glob arrays

Control which files the linter walks:

```toml
includes = ["shaders/**/*.hlsl", "src/gpu/**/*.hlsl"]
excludes = ["shaders/third_party/**", "shaders/generated/**"]
```

When `includes` is absent, every `.hlsl` file under the invocation
directory is considered (subject to `excludes`).

## `[[overrides]]` — per-directory rule tuning

The `[[overrides]]` array allows different severity for specific
directory subtrees:

```toml
[[overrides]]
paths = ["shaders/experimental/**"]
[overrides.rules]
derivative-in-divergent-cf = "warning"

[[overrides]]
paths = ["shaders/vendor/**"]
[overrides.rules]
pow-const-squared  = "off"
redundant-saturate = "off"
```

Overrides are applied in source order; later matches win.

## Walk-up resolution

The CLI walks up the directory tree from each file's parent looking for
`.hlsl-clippy.toml`. The walk stops at the first `.git/` ancestor. If
no `.git/` is present (e.g. you extracted a tarball with no
repository), the walk continues to the filesystem root — set
`--config <path>` explicitly in that case to avoid surprises.

## Inline suppression

### Line scope

Suppress a single rule on the same line:

```hlsl
float k = pow(x, 2.0);  // hlsl-clippy: allow(pow-const-squared)
```

Or the next non-comment line:

```hlsl
// hlsl-clippy: allow(pow-const-squared)
float k = pow(x, 2.0);
```

Multiple rules in one comment: `allow(pow-const-squared, redundant-saturate)`.

### Block scope

Place an `allow(...)` directive immediately before an opening brace
to suppress the named rules for the entire `{...}` block:

```hlsl
// hlsl-clippy: allow(redundant-saturate)
{
    float a = saturate(saturate(x));   // suppressed
    float b = saturate(y);             // suppressed
}
float c = saturate(saturate(z));       // NOT suppressed
```

### File scope

Place an `allow(...)` directive before any non-comment token at the
top of the file:

```hlsl
// hlsl-clippy: allow(*)
// Vendor file we do not control.
```

`allow(*)` suppresses every rule for the file. Individual rules can
also be listed: `allow(pow-const-squared, redundant-saturate)`.

## Example configs

### Minimal — opt one rule into "error"

```toml
[rules]
derivative-in-divergent-cf = "error"
```

### Typical project layout

```toml
[rules]
pow-const-squared           = "warn"
redundant-saturate          = "warn"
clamp01-to-saturate         = "off"

includes = ["shaders/**/*.hlsl"]
excludes = ["shaders/third_party/**"]

[[overrides]]
paths = ["shaders/experimental/**"]
[overrides.rules]
pow-const-squared = "off"
```

### Strict — every rule at "warning", a few promoted to "error"

```toml
[[overrides]]
paths = ["**/*.hlsl"]
[overrides.rules]
derivative-in-divergent-cf  = "error"
barrier-in-divergent-cf     = "error"
wave-intrinsic-non-uniform  = "error"
```

## Experimental flags

```toml
[experimental]
work-graph-mesh-nodes = true
```

Gates preview rule packs whose target API hasn't stabilised yet (per
ADR 0010). Off by default; opt in if you author SM 6.9 mesh-node
shaders.
