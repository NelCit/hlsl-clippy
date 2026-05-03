---
title: Getting started
outline: deep
---

# Getting started

This page covers a fresh install + first lint in five minutes.

## Install

### From a [GitHub Release](https://github.com/NelCit/shader-clippy/releases)

Each tagged release attaches per-platform archives. Pick the one for
your OS, extract, and put the `shader-clippy` CLI on `PATH`:

| Platform        | Asset                                                |
|-----------------|------------------------------------------------------|
| Linux x86_64    | `shader-clippy-<version>-linux-x86_64.tar.gz`          |
| Windows x86_64  | `shader-clippy-<version>-windows-x86_64.zip`           |
| macOS aarch64   | `shader-clippy-<version>-macos-aarch64.tar.gz`         |

Each archive contains the CLI + the `shader-clippy-lsp` server +
`LICENSE` / `NOTICE` / `THIRD_PARTY_LICENSES.md`.

Verify the install:

```sh
shader-clippy --version    # prints e.g. 0.5.3
```

### VS Code extension

Search for **Shader Clippy** by `nelcit` in the Extensions view, or:

```sh
code --install-extension nelcit.shader-clippy
```

The LSP binary is bundled inside the per-platform `.vsix`, so the
extension Just Works on first activation — no extra download, no
PATH configuration. See [LSP / IDE](/lsp) for editor-specific
options.

### From source

If you want to track `main` or contribute rules, see the from-source
build steps in the
[repo README](https://github.com/NelCit/shader-clippy#from-source).

## Hello-world lint

```sh
shader-clippy lint shader.hlsl
```

Sample output for a shader with a `pow(x, 2.0)`:

```
shader.hlsl:42:14: warning: pow(x, 2.0) is equivalent to x*x [pow-const-squared]
  42 |     float attenuation = pow(distance, 2.0);
                  ^^^^^^^^^^^^^^^^^^^^^^^^
```

Apply machine-applicable fixes in place:

```sh
shader-clippy lint --fix shader.hlsl
```

Exit codes: `0` clean, `1` warnings emitted, `2` errors or invocation
failure.

## Output formats

| Flag                            | Format                                                                |
|---------------------------------|-----------------------------------------------------------------------|
| (default)                       | rustc-style with caret line                                           |
| `--format=json`                 | flat JSON array, stable schema (CI gates parse this)                  |
| `--format=github-annotations`   | GitHub Actions `::warning file=...:: msg` lines (auto on `$GITHUB_ACTIONS`) |

## First config

Drop a `.shader-clippy.toml` next to your shader tree. The CLI walks up
from each file's parent until it finds one (bounded by `.git/`).

```toml
[rules]
pow-const-squared    = "warn"
redundant-saturate   = "warn"
clamp01-to-saturate  = "off"

includes = ["shaders/**/*.hlsl"]
excludes = ["shaders/third_party/**"]
```

Full reference: [Configuration](/configuration).

## Where to next

- Browse the [rules catalog](/rules/) — every rule has a GPU-mechanism
  explanation.
- Read the [launch blog series](/blog/) — one post per rule category.
- Wire `shader-clippy` as a [CI gate](/ci).
- Drop into your editor via the [LSP](/lsp).
