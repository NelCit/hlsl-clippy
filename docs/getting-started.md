# Getting Started

> **Status:** pre-v0 — install instructions will land with the v0.1 release. See [ROADMAP](../ROADMAP.md).

## Installation

Pre-v0; install instructions will land with the v0.1 release.

Planned distribution: a single static binary per OS via GitHub Releases (Windows, Linux; macOS in a later phase). No runtime dependencies beyond the Slang shared library, which will ship alongside the binary.

## Hello-world lint invocation

Once v0.1 ships, linting a single shader file looks like:

```sh
hlsl-clippy lint shaders/main.hlsl
```

Linting an entire directory tree:

```sh
hlsl-clippy lint shaders/
```

Expected output format (plain text, pre-v0 design):

```
shaders/main.hlsl:12:18: warning[pow-to-mul]: pow(x, 2.0) can be simplified to x*x
  --> shaders/main.hlsl:12:18
   |
12 |     float k = pow(x, 2.0);
   |               ^^^^^^^^^^^
   = help: replace with `x * x`
```

Pass `--format=json` for machine-readable output (useful in CI — see [ci.md](ci.md)).

## Basic configuration

`hlsl-clippy` reads `.hlsl-clippy.toml` from the directory it is invoked in, or from any ancestor directory up to the root of the workspace.

A minimal config that promotes one rule to a hard error:

```toml
[rules]
pow-to-mul = "deny"
```

Full configuration reference: [docs/configuration.md](configuration.md).
