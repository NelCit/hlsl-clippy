# hlsl-clippy

> Performance + correctness rules for HLSL -- beyond what `dxc` catches.

[![CI](https://github.com/NelCit/hlsl-clippy/actions/workflows/ci.yml/badge.svg)](https://github.com/NelCit/hlsl-clippy/actions/workflows/ci.yml)
[![Lint](https://github.com/NelCit/hlsl-clippy/actions/workflows/lint.yml/badge.svg)](https://github.com/NelCit/hlsl-clippy/actions/workflows/lint.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![Docs (CC-BY-4.0)](https://img.shields.io/badge/Docs-CC--BY--4.0-orange)](LICENSE)

<!-- TODO: demo gif -->

## What it is

Static linter for HLSL. AST via tree-sitter-hlsl, compile + reflection via
Slang. **154 rules** across math, bindings, texture, workgroup, control-flow,
mesh, DXR, work-graphs, SER, cooperative-vector, long-vectors, opacity-
micromaps, sampler-feedback, VRS, and wave-helper-lane. Surfaces portable
anti-patterns that `dxc` and vendor analyzers don't flag -- patterns rooted
in the GPU hardware (RDNA, NVIDIA, Xe-HPG).

## Quick demo

```hlsl
// Bad: pow(x, 2.0) lowers to a transcendental on every IHV.
float attenuation = pow(distance, 2.0);

// Good: same result, free.
float attenuation = distance * distance;
```

```sh
$ hlsl-clippy lint shader.hlsl
shader.hlsl:42:14: warning: pow(x, 2.0) is equivalent to x*x [pow-const-squared]
  42 |     float attenuation = pow(distance, 2.0);
                  ^^^^^^^^^^^^^^^^^^^^^^^^
$ hlsl-clippy lint --fix shader.hlsl
hlsl-clippy: applied 1 fix to shader.hlsl
```

## Why it exists

Vendor analyzers (RGA, Nsight) have ground truth on their ISA but only see
one IHV. `dxc` catches syntax errors, not perf footguns. `hlsl-clippy` is
the missing portable middle layer -- and every rule's doc page explains the
GPU mechanism so the warning doubles as a teaching tool.

## Install

### From source

**Prerequisites:** CMake 3.20+, C++23 compiler (MSVC 19.44+, Clang 18+,
GCC 14+).

```sh
git clone --recurse-submodules https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy
cmake -B build && cmake --build build
./build/cli/hlsl-clippy lint shader.hlsl
```

On Windows, `tools\dev-shell.ps1` sets up MSVC + the Slang prebuilt cache
in one shot.

### VS Code extension

Coming with v0.5 (sub-phase 5e). Until then: clone, build, and point your
editor at `./build/lsp/hlsl-clippy-lsp[.exe]`.

### Marketplace

Available once the v0.5 release tag ships (gates on
`.github/workflows/release-vscode.yml`).

## CLI usage

```sh
hlsl-clippy lint shader.hlsl                          # warn-and-report
hlsl-clippy lint --fix shader.hlsl                    # apply machine-applicable fixes
hlsl-clippy lint --target-profile sm_6_8 shader.hlsl  # override Slang profile
hlsl-clippy lint --config path/.hlsl-clippy.toml shader.hlsl
hlsl-clippy lint --format=json shader.hlsl            # flat JSON array, stable schema
hlsl-clippy lint --format=github-annotations s.hlsl   # GitHub Actions ::warning lines
```

Exit codes: `0` clean, `1` warnings, `2` errors or invocation failure.

When `$GITHUB_ACTIONS=true` and `--format` is unset, `github-annotations`
is selected automatically — drop a copy of [docs/ci/lint-hlsl-example.yml](docs/ci/lint-hlsl-example.yml)
into `.github/workflows/` and inline annotations show up on PR diffs.

## Configuration (`.hlsl-clippy.toml`)

Drop a `.hlsl-clippy.toml` next to your shader tree. The CLI walks up from
each file's parent until it hits one (bounded by `.git/`).

```toml
[rules]
pow-const-squared    = "warn"
redundant-saturate   = "warn"
clamp01-to-saturate  = "off"

includes = ["shaders/**/*.hlsl"]
excludes = ["shaders/third_party/**"]

[[overrides]]
paths = ["shaders/experimental/**"]
[overrides.rules]
pow-const-squared = "off"
```

Full schema: [docs/configuration.md](docs/configuration.md).

## Browse rules

<!-- TODO: enable docs site link once sub-phase 5e ships and the docs-site agent lands -->
Per-rule pages with GPU rationale, examples, and fix availability live
under [docs/rules/](docs/rules/) (catalog: [docs/rules/index.md](docs/rules/index.md)).
A hosted docs site at `https://nelcit.github.io/hlsl-clippy/` lands with v0.5.

## Companion blog series

Every rule ships with a long-form blog post explaining the GPU mechanism --
cycle counts, occupancy impact, microarchitecture details. See
[docs/blog/](docs/blog/) (CC-BY-4.0).

## Contributing

- DCO sign-off on every commit: `git commit -s`.
- Conventional Commits 1.0 (`feat:`, `fix:`, `docs:`, ...).
- Run `tools/install-hooks.sh` (or `tools\install-hooks.ps1` on Windows)
  before your first PR.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the rule-authoring guide.

## License

Code is [Apache-2.0](LICENSE). Documentation, blog posts, and rule pages
are [CC-BY-4.0](LICENSE). Vendored dependencies retain their own licenses
(see [NOTICE](NOTICE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)).
