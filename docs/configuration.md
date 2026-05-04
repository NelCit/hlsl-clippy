---
title: Configuration
outline: deep
---

# Configuration

`shader-clippy` reads `.shader-clippy.toml` from the directory of each
linted file (or any ancestor directory up to the first `.git/`
parent). When no config file is found, all rules run at their
built-in default severity.

The CLI also accepts an explicit path:

```sh
shader-clippy lint --config path/to/.shader-clippy.toml shader.hlsl
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

## `[lint] source-language` — frontend selection (v1.3+)

Selects which language frontend the orchestrator engages for files
matched by this config (added in v1.3.0; ADR 0020 sub-phase A).

```toml
[lint]
source-language = "auto"   # default — infer from file extension
# source-language = "hlsl" # force HLSL frontend on every file
# source-language = "slang" # force Slang frontend on every file
```

| Value     | Behaviour                                                   |
|-----------|-------------------------------------------------------------|
| `"auto"`  | Infer from file extension. `.slang` → Slang, otherwise HLSL.|
| `"hlsl"`  | Force tree-sitter-hlsl + Slang's HLSL frontend.             |
| `"slang"` | Skip tree-sitter parse; only run reflection-stage rules.    |

When the resolved language is Slang, the orchestrator skips AST + control-
flow + IR rule dispatch and emits a one-shot `clippy::language-skip-ast`
informational diagnostic per source. Tree-sitter-hlsl cannot parse Slang's
language extensions (`__generic`, `interface`, `extension`,
`associatedtype`, `import`); tree-sitter-slang integration tracks for
v1.4+. **Reflection-stage rules are also quarantined for v1.3.0** because
the Slang reflection bridge crashes on `.slang` ingestion under the
v1.3-pinned Slang prebuilt — the quarantine lifts in v1.3.x once the
bridge's call-suffixed virtual_path scheme is hardened. The full plan
lives in [ADR 0020](https://github.com/NelCit/shader-clippy/blob/main/docs/decisions/0020-slang-language-compatibility.md).

The CLI also accepts `--source-language=<auto|hlsl|slang>` per invocation;
the flag overrides the TOML value when set.

To silence the per-source notice in CI gate-mode logs, suppress
`clippy::language-skip-ast`:

```toml
[rules]
"clippy::language-skip-ast" = "allow"
```

Or per-source:

```hlsl
// shader-clippy: allow(clippy::language-skip-ast)
```

## `[shader] include-directories` - Slang reflection include roots

Reflection-aware rules compile the shader through Slang. Add include roots
here when your project uses logical include paths such as
`#include <utils.hlsli>`:

```toml
[shader]
include-directories = [
  "donut/include",
  "sources/light_propagation_engine/shaders",
]
```

Relative paths are resolved against the directory containing
`.shader-clippy.toml`. The VS Code extension also supports
`shaderClippy.includeDirectories` for editor-only configuration.

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
`.shader-clippy.toml`. The walk stops at the first `.git/` ancestor. If
no `.git/` is present (e.g. you extracted a tarball with no
repository), the walk continues to the filesystem root — set
`--config <path>` explicitly in that case to avoid surprises.

## Inline suppression

### Line scope

Suppress a single rule on the same line:

```hlsl
float k = pow(x, 2.0);  // shader-clippy: allow(pow-const-squared)
```

Or the next non-comment line:

```hlsl
// shader-clippy: allow(pow-const-squared)
float k = pow(x, 2.0);
```

Multiple rules in one comment: `allow(pow-const-squared, redundant-saturate)`.

### Block scope

Place an `allow(...)` directive immediately before an opening brace
to suppress the named rules for the entire `{...}` block:

```hlsl
// shader-clippy: allow(redundant-saturate)
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
// shader-clippy: allow(*)
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
