# hlsl-clippy

> Performance + correctness rules for HLSL -- beyond what `dxc` catches.

[![CI](https://github.com/NelCit/hlsl-clippy/actions/workflows/ci.yml/badge.svg)](https://github.com/NelCit/hlsl-clippy/actions/workflows/ci.yml)
[![Lint](https://github.com/NelCit/hlsl-clippy/actions/workflows/lint.yml/badge.svg)](https://github.com/NelCit/hlsl-clippy/actions/workflows/lint.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![Docs (CC-BY-4.0)](https://img.shields.io/badge/Docs-CC--BY--4.0-orange)](LICENSE)

<!-- Demo recording: `vhs` script to (re-)generate is at docs/assets/demo.tape; hosted gif lands with v0.5.1 once the recording is captured. -->

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

### Prebuilt CLI / LSP binaries

Each tagged release attaches per-platform archives to its
[GitHub Release](https://github.com/NelCit/hlsl-clippy/releases) page:

- `hlsl-clippy-<version>-linux-x86_64.tar.gz`
- `hlsl-clippy-<version>-windows-x86_64.zip`
- `hlsl-clippy-<version>-macos-aarch64.tar.gz`

Each archive contains the `hlsl-clippy` CLI + `hlsl-clippy-lsp` LSP
server + `LICENSE` / `NOTICE` / `THIRD_PARTY_LICENSES.md`. Drop on
`PATH` and run.

### From source

**Prerequisites:** CMake 3.20+, C++23 compiler (MSVC 19.44+ /
VS 17.14 / Build Tools 14.44, Clang 18+ with libc++ 17+ or
libstdc++ 13+, GCC 14+). Per-platform first-time setup:

- **Windows:** install Visual Studio 2022 17.14+ with the
  "Desktop development with C++" workload. Dot-source
  `tools\dev-shell.ps1` to enter the MSVC dev shell + put VS-bundled
  CMake / Ninja / Slang DLLs on PATH.
- **Linux (Ubuntu 24.04+):** `wget -qO- https://apt.llvm.org/llvm.sh | sudo bash -s -- 18 all && sudo apt-get install -y libc++-18-dev libc++abi-18-dev cmake ninja-build`.
- **macOS (Apple Silicon):** `brew install cmake ninja llvm@18` then `export PATH="/opt/homebrew/opt/llvm@18/bin:$PATH"` (Apple's bundled Clang is too old for C++23).

```sh
git clone --recurse-submodules https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy

# Bootstrap the Slang prebuilt cache (skip the ~20-min from-source build):
bash  tools/fetch-slang.sh         # Linux / macOS
pwsh  tools/fetch-slang.ps1        # Windows

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/cli/hlsl-clippy lint shader.hlsl
```

### VS Code extension

Install **HLSL Clippy** by `nelcit` from the Marketplace, or:

```sh
code --install-extension nelcit.hlsl-clippy
```

The extension ships the `hlsl-clippy-lsp` binary inside the per-platform
`.vsix` (linux-x64 / win32-x64 / darwin-arm64). Marketplace serves the
matching one for your OS automatically; no extra download or PATH
configuration needed. Per-platform `.vsix` files are also attached to
every [GitHub Release](https://github.com/NelCit/hlsl-clippy/releases)
for sideload.

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

Hosted docs site: **<https://nelcit.github.io/hlsl-clippy/>** — per-rule
pages with GPU rationale, examples, and fix availability, plus the
companion blog. The same content also lives in-tree under
[docs/rules/](docs/rules/) (catalog: [docs/rules/index.md](docs/rules/index.md))
for offline reading.

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
