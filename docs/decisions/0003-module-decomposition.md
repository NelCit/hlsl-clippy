---
status: Proposed
date: 2026-04-30
deciders: NelCit
tags: [architecture, build-system, phase-1]
---

# Module decomposition

## Context and Problem Statement

The current source tree is `crates/cli/` + `crates/core/` (the `crates/` naming is a Rust-ism left over from project bootstrap; renaming to `cli/` + `core/` is already on the Phase 0 task list in `ROADMAP.md`).

Once we start adding rules, three forces push back on a flat `cli/` + `core/`:

1. The rule engine should not transitively pull Slang headers into a tree-sitter-only translation unit.
2. The future LSP server (Phase 5) wants to link the same rule + diagnostic engine the CLI links.
3. ABI isolation: when Slang or tree-sitter bumps, only the matching backend module should recompile.

The architecture review proposes a finer split. This ADR captures that proposal **without** silently overriding the rename agent's shipped cli/core layout — the user should pick the moment to commit to the larger restructure.

## Decision Drivers

- Public-header backend isolation (no `slang.h` or `tree_sitter/api.h` in `include/hlslc/`).
- Avoid linking unused backends into a TU (rules that only need the AST shouldn't pull in Slang).
- Keep CLI + LSP linking the same artifacts.
- Per-rule cost: adding rule N should be one file plus one CMake glob, not a CMake patch in five places.

## Considered Options

### Option A — Shipped layout (current): `cli/` + `core/`

```
shader-clippy/
  CMakeLists.txt
  cli/
    src/main.cpp
  core/
    include/hlslc/...
    src/...
  tests/
```

Two libraries, simple build. Rules + parser + semantic backends all live inside `core` and can `#include` each other freely.

### Option B — Architecture-review proposal: granular libs

```
shader-clippy/
  CMakeLists.txt
  cmake/                         # FindSlang.cmake, warnings.cmake, sanitizers.cmake
  third_party/                   # vendored: slang/, tree-sitter/, tree-sitter-hlsl/
  include/hlslc/                 # PUBLIC headers, namespace hlslc::
    source.hpp  span.hpp  diagnostic.hpp  fix.hpp
    rule.hpp  rule_registry.hpp  config.hpp  driver.hpp
  libs/
    parser/      (hlslc_parser)      # tree-sitter wrapper, query helpers
    semantic/    (hlslc_semantic)    # Slang session, reflection bridge
    diag/        (hlslc_diag)        # Diagnostic, Fix, Rewriter, formatters
    rules/       (hlslc_rules)       # individual rules + registry glue
    driver/      (hlslc_driver)      # pipeline: source -> parse -> compile -> rules
  apps/
    cli/         (shader-clippy)
    lsp/         (shader-clippy-lsp)   # Phase 5
  tests/  unit/  corpus/  golden/
```

Dependency graph: `rules → parser + semantic + diag`, `cli` and `lsp → driver`. Each rule lives in `libs/rules/<category>/<rule_name>.cpp` and self-registers via static initializer collected in `rule_registry.cpp`.

## Decision Outcome

**Status: Proposed for Phase 1+.**

The shipped `cli/` + `core/` split (Option A) stays for Phase 0 and the first Phase 1 work. Option B is the target structure to adopt **once the rule count or LSP work makes the granularity pay for itself** — explicitly:

- After the first ~10 rules land and the rule-registry boilerplate becomes a real file-count problem; **or**
- When the LSP server starts (Phase 5), at which point sharing the driver between two `apps/` is the cleanest outcome; **or**
- When Slang ABI churn forces enough recompilation that isolating `libs/semantic/` matters.

This ADR exists so the granular layout is a deliberate decision the user makes, not a silent override of the rename agent's work.

### Consequences (if/when adopted)

Good:

- Rules never see `<slang.h>` or `<tree_sitter/api.h>`. Backend bumps recompile only `libs/parser/` or `libs/semantic/`.
- Adding rule #47 is one file under `libs/rules/<category>/` plus one CMake glob.
- `cli` and `lsp` link the same `driver`; no parallel rule-engine implementations.
- `.clang-tidy` `HeaderFilterRegex` becomes `^(apps|libs|include)/.*\.(h|hpp)$` — enforces public-header narrowing.

Bad:

- Bigger CMake graph; contributors have more files to navigate.
- Static initializers for rule self-registration interact with link order; we must compile rules into an object library and force-link that into the registry consumer (`-Wl,--whole-archive`/`/WHOLEARCHIVE`).
- Migration cost — moving sources, updating includes, retraining clang-tidy filter — is a one-shot but not free.

### Confirmation

When the migration happens:

- Public header inventory must consist only of files in `include/hlslc/`.
- A CI grep enforces no `slang.h` / `tree_sitter/*.h` in `include/hlslc/`.
- Both `apps/cli/main.cpp` and `apps/lsp/main.cpp` link `hlslc_driver` (and only `hlslc_driver` from first-party libs).

## Pros and Cons of the Options

### Option A — `cli/` + `core/` (current)

- Good: minimal CMake; everything in two targets.
- Good: the rename is already done in this branch; matches the ROADMAP Phase 0 task.
- Bad: rules and Slang/tree-sitter code share a translation-unit boundary; nothing prevents an AST-only rule from including `slang.h`.
- Bad: future LSP must either copy the rule engine or refactor `core` anyway.

### Option B — `include/hlslc/` + `libs/{parser,semantic,diag,rules,driver}/` + `apps/{cli,lsp}/`

- Good: enforces the public-API boundary by structure, not by review.
- Good: scales to many rules without making `core/` a dumping ground.
- Good: shared `driver` is the natural LSP integration point.
- Bad: heavier CMake; static-init link gymnastics; migration cost.
- Bad: premature for Phase 0 — most of the cost is paid before any rule is written.

## Links

- Verbatim research: `_research/architecture-review.md` §1 (module decomposition), §3 (API boundaries), §5 (decisions to lock in).
- Related: ADR 0001 (Slang), ADR 0002 (tree-sitter-hlsl), ADR 0005 (CI/CD — affects warning-flag interface library design).
