# Contributing to hlsl-clippy

Thank you for your interest in contributing. This document covers everything needed to go from zero to a merged pull request.

> **Status:** pre-v0. The codebase is a scaffold. The best contributions right now are: rule proposals (open an issue), test fixture additions, documentation improvements, and CI setup (Phase 0 tasks from ROADMAP.md).

## Table of contents

- [Code of conduct](#code-of-conduct)
- [Dev setup](#dev-setup)
- [Build commands](#build-commands)
- [Test commands](#test-commands)
- [Lint commands](#lint-commands)
- [Commit messages](#commit-messages)
- [DCO requirement](#dco-requirement)
- [Branch naming](#branch-naming)
- [Pull requests](#pull-requests)
- [Authoring a rule](#authoring-a-rule)
- [Why Apache-2.0](#why-apache-20)
- [Where to start](#where-to-start)

---

## Code of conduct

This project follows the [Contributor Covenant 2.1](CODE_OF_CONDUCT.md). By participating, you agree to abide by its terms. Reports go to `[TODO: maintainer contact]`.

---

## Dev setup

**Prerequisites:**

| Tool | Minimum version | Notes |
|---|---|---|
| Git | Any recent | |
| CMake | 3.20 | |
| MSVC | 2022 (17.x) | Windows primary target |
| Clang | 16 | Linux CI target |
| GCC | 13 | Alternative on Linux |
| clang-format | 16 | Code formatting |
| clang-tidy | 16 | Static analysis |

Optional but recommended:

- `ninja` — faster builds than the default generator
- `ccache` — speeds up rebuilds
- A C++ IDE with CMake integration (VS Code with CMake Tools, CLion, Visual Studio)

**Clone:**

```sh
git clone https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy
```

---

## Build commands

```sh
# Configure (release)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Configure (debug, with compile_commands.json for clangd)
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build build

# Run the binary
./build/hlsl-clippy --help
```

On Windows with MSVC:

```cmd
cmake -B build
cmake --build build --config Release
.\build\Release\hlsl-clippy.exe --help
```

---

## Test commands

> Tests are planned for Phase 1. This section will be updated when the test infrastructure lands.

The test suite uses fixture-based testing: HLSL files under `tests/fixtures/` are the test inputs. Each fixture directory will contain:

- `input.hlsl` — the file to lint
- `expected.txt` or `expected.json` — the expected diagnostic output

```sh
# Run all tests (once implemented)
ctest --test-dir build --output-on-failure

# Run tests for a specific rule
ctest --test-dir build -R pow-const-squared
```

When adding a new rule, add fixtures under `tests/fixtures/<rule-id>/`. See [docs/architecture.md](docs/architecture.md) for the fixture format.

---

## Lint commands

```sh
# Format check (must pass before commit)
clang-format --dry-run --Werror src/**/*.cpp src/**/*.h

# Apply formatting
clang-format -i src/**/*.cpp src/**/*.h

# Static analysis (once compile_commands.json exists)
clang-tidy src/**/*.cpp -- -I.
```

The clang-format and clang-tidy configurations are in `.clang-format` and `.clang-tidy` at the project root.

---

## Commit messages

This project follows [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/).

Format:

```
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
Signed-off-by: Full Name <email@example.com>
```

**Types:**

| Type | When to use |
|---|---|
| `feat` | A new rule, feature, or user-visible capability |
| `fix` | A bug fix |
| `docs` | Documentation only |
| `refactor` | Code change that is neither a fix nor a feature |
| `test` | Adding or updating tests |
| `build` | CMake, CI, dependency changes |
| `chore` | Other maintenance (version bumps, clang-format runs) |

**Scope** (optional): the component affected, e.g., `rule-engine`, `cli`, `pow-const-squared`, `docs`.

**Examples:**

```
feat(rule-engine): add Rule interface and AST visitor dispatch

fix(pow-const-squared): handle exponent expressed as integer literal

docs: add pow-const-squared rule page

build: add DXC as FetchContent dependency
```

Breaking changes: add `!` after the type and a `BREAKING CHANGE:` footer.

---

## DCO requirement

Every commit must include a `Signed-off-by` line asserting that you have the right to submit the code under the project license. This is the [Developer Certificate of Origin (DCO)](https://developercertificate.org/), version 1.1. There is no CLA.

Add it with:

```sh
git commit -s -m "feat: add pow-const-squared rule"
```

Or add it manually to the commit message:

```
Signed-off-by: Your Full Name <your@email.com>
```

Pull requests with commits missing `Signed-off-by` will not be merged.

---

## Branch naming

| Pattern | Purpose |
|---|---|
| `feat/<description>` | New feature or rule |
| `fix/<description>` | Bug fix |
| `docs/<description>` | Documentation change |
| `refactor/<description>` | Refactoring |
| `build/<description>` | Build system or CI |

Examples: `feat/pow-const-squared`, `docs/rule-template`, `build/dxc-fetchcontent`.

---

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make your changes, following the coding style (clang-format enforces it).
3. Add or update tests in `tests/fixtures/` for any rule changes.
4. Update the rule page in `docs/rules/` if you are adding or changing a rule.
5. Ensure all commits are signed off (`Signed-off-by`).
6. Open a pull request using the [PR template](.github/PULL_REQUEST_TEMPLATE.md).

**PR title format:** `<type>(<scope>): <description>` — matches Conventional Commits.

PRs that add a rule without a corresponding `docs/rules/<rule-id>.md` page will not be merged. Use [docs/rules/_template.md](docs/rules/_template.md).

---

## Authoring a rule

This is a walkthrough for adding a new lint rule. Rules that do not yet have a working rule engine (Phases 2+) should be proposed as issues first.

### Step 1 — Propose the rule

Open a [rule proposal issue](https://github.com/NelCit/hlsl-clippy/issues/new?template=rule_proposal.yml). The template requires:

- Rule name (kebab-case ID)
- Category (`math`, `bindings`, `texture`, `control-flow`, `performance`)
- What it detects (specific pattern)
- Why it matters on a GPU (hardware reasoning — mandatory)
- Example bad and good HLSL code
- Suggested phase

Get a maintainer thumbs-up before implementing.

### Step 2 — Write the rule page

Copy `docs/rules/_template.md` to `docs/rules/<rule-id>.md` and fill in every section. The "Why it matters on a GPU" section is mandatory — it is the blog-post seed that makes each rule defensible and useful.

### Step 3 — Implement the rule

> The rule API does not exist yet (Phase 1). This step describes the intended pattern.

Create `src/rules/<rule-id>.cpp`. Implement the `Rule` interface:

```cpp
// src/rules/pow_const_squared.cpp  (illustrative — API not finalized)
#include "rule.h"

class PowConstSquared : public Rule {
public:
    std::string_view id() const override { return "pow-const-squared"; }
    Severity default_severity() const override { return Severity::Warn; }

    void visit(const CallExpr& call, Context& ctx) override {
        if (!is_pow_call(call)) return;
        if (!is_constant_integer(call.arg(1), 2)) return;
        ctx.emit(Diagnostic{
            .rule_id = id(),
            .span    = call.span(),
            .message = "pow(x, 2.0) is equivalent to x*x",
            .fix     = make_multiply_fix(call),
        });
    }
};
```

### Step 4 — Add test fixtures

Create `tests/fixtures/<rule-id>/` with:

- `bad.hlsl` — code that should trigger the rule
- `good.hlsl` — code that should not trigger the rule
- `expected.txt` — the expected diagnostic output for `bad.hlsl`

### Step 5 — Register the rule

Add the rule to the rule registry (location TBD once Phase 1 is complete).

### Step 6 — Open a PR

Fill in the [PR template](.github/PULL_REQUEST_TEMPLATE.md), including the "Rule docs" section. Link the rule proposal issue.

---

## Why Apache-2.0

The project uses the Apache License, Version 2.0 rather than MIT.

The primary reason is patent protection. Apache-2.0 includes an explicit patent grant: contributors grant users a license to any patents they hold that are necessarily infringed by their contribution. MIT contains no such grant. For a tool that may incorporate techniques from graphics-hardware vendor documentation or shader compiler research, this grant gives downstream users (game studios, middleware vendors, engine teams) a cleaner IP position than MIT alone.

Apache-2.0 is also the license used by several projects in this ecosystem (LLVM, DXC components, many Rust crates). This minimizes license-compatibility friction when vendoring or linking those dependencies.

---

## Where to start

**For first-time contributors:**

- Issues labelled [`good first issue`](https://github.com/NelCit/hlsl-clippy/labels/good%20first%20issue) — small, well-scoped tasks.
- Documentation improvements — the `docs/` directory is the seed of the docs site; errors, unclear phrasing, and missing content are all welcome fixes.
- Test fixture additions — add `tests/fixtures/` examples for patterns you want to see covered.

**For graphics engineers:**

- [Rule proposals](https://github.com/NelCit/hlsl-clippy/issues/new?template=rule_proposal.yml) — if you know a portable HLSL pattern that hurts GPU performance or hides a correctness bug, open a proposal. The "why it matters on a GPU" section is where your knowledge has the most impact.

**For tooling contributors:**

- Phase 0 tasks: DXC integration, CI setup (Windows MSVC + Linux Clang). See [ROADMAP.md](ROADMAP.md).
