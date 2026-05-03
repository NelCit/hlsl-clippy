# AGENTS.md — shared brief for dispatched coding agents

Read this file FIRST. It captures the conventions, tool paths, and constraints
every rule-pack / wiring / infra agent needs. Skip the constraints and you
will burn a CI cycle. The orchestrator dispatches you with a per-task prompt
that points at this file; assume the orchestrator already gave you the task
specifics — what's below is the cross-task boilerplate.

---

## 1. File-naming conventions

- Rule source: `core/src/rules/<snake_id>.cpp` (e.g., `redundant_normalize.cpp`)
- Rule test: `tests/unit/test_<snake_id>.cpp` (e.g., `test_redundant_normalize.cpp`)
- Factory function: `shader_clippy::rules::make_<snake_id>()` returning `std::unique_ptr<Rule>`. The basename of `<snake_id>.cpp` and the suffix after `make_` MUST match exactly. The wiring agent greps for this; mismatches break the build.
- Doc page: `docs/rules/<kebab-id>.md`. Kebab matches the rule-id string used inside `class Rule { id() { return "kebab-id"; } }`. The kebab-to-snake mapping is mechanical: `byteaddressbuffer-load-misaligned` ↔ `byteaddressbuffer_load_misaligned`.

## 2. Rule shape (canonical exemplars to pattern-match)

| Stage | Exemplar | Public APIs used |
|---|---|---|
| `Stage::Ast` | `core/src/rules/lerp_extremes.cpp` | `AstTree`, tree-sitter via `parser_internal.hpp`, optional declarative TSQuery via `query/query.hpp` |
| `Stage::Reflection` | (none yet shipped — read `core/include/shader_clippy/reflection.hpp` + `core/src/rules/util/reflect_*.hpp`) | `ReflectionInfo`, `reflect_resource::*`, `reflect_sampler::*`, `reflect_stage::*` |
| `Stage::ControlFlow` | (Pack 4c-A through 4c-E) | `ControlFlowInfo`, `cfg_query::*`, `uniformity::*`, `light_dataflow::*`, `helper_lane_analyzer::*` |

When in doubt about a rule shape, copy the closest exemplar verbatim and edit the detection logic. The framing (`namespace shader_clippy::rules { namespace { ... } make_*() { ... } }`) is identical across all rules.

## 3. Hard constraints (CI enforces; violations break the build)

- **No `<slang.h>` outside `core/src/reflection/slang_bridge.cpp`.** The `lint.yml` grep step fails the lint job if `<slang.h>` appears in `core/include/shader_clippy/`, `core/src/rules/`, or `lsp/src/`.
- **No `<tree_sitter/api.h>` in public headers.** Same scope as Slang. Internal TUs only.
- **No exceptions across the `core` API boundary.** Use `std::expected<T, Diagnostic>`. Catch internally if needed.
- **No raw `new`/`delete` outside explicit ownership boundaries.** RAII; `std::unique_ptr` / `std::shared_ptr` default.
- **MSVC `/W4 /WX /permissive-` + Clang `-Wall -Wextra -Wpedantic -Werror` apply to every first-party TU.** Common gotchas:
  - C4100 unused parameter → `[[maybe_unused]]` on the param
  - C4530 missing `/EHsc` → CMakeLists must add the flag (only an issue for new targets, not new rule files)
  - C2737 `const` object must be initialised → write `const T x{};` not `const T x;`
- **C++23 stdlib feature-test guards.** Don't include `<flat_map>`, `<print>`, `<inplace_vector>` directly — wrap in `#if defined(__cpp_lib_<feature>) && __cpp_lib_<feature> >= NNN` with a `std::map` / `std::format` / `std::vector` fallback. Pattern: see `core/src/reflection/engine.hpp` lines 21-37 for the canonical block. libstdc++ 13 (Ubuntu 24.04 CI) ≠ MSVC 19.50 on stdlib coverage.
- **`Span` and `ByteSpan` API.** `core/include/shader_clippy/source.hpp` defines: `struct Span { SourceId source; ByteSpan bytes; }` and `struct ByteSpan { uint32_t lo, hi; size(); empty(); }`. Access is `span.bytes.lo` / `.bytes.hi` / `.bytes.size()`, NOT `span.lo` directly. `tree.byte_range(node)` returns a `ByteSpan` (not a `Span`). To construct: `Span{.source = tree.source_id(), .bytes = ByteSpan{.lo = lo, .hi = hi}}` OR reuse a pre-built `Span` from a binding's `declaration_span` field.

## 4. Mandatory clang-format step (per-agent, non-negotiable)

Before reporting back, every code-writing agent runs:

```
"C:/Program Files (x86)/Android/AndroidNDK/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang-format.exe" -i <files-you-modified>
```

Then verify:

```
"C:/Program Files (x86)/Android/AndroidNDK/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang-format.exe" --dry-run --Werror <files-you-modified>
```

Dry-run must exit `0`. If your environment doesn't have the Android NDK clang-format, search for any `clang-format-18` (the CI version) or `clang-format` 18.x. Do NOT use clang-format 19+ or 17- — output drift will fail CI.

This step is the single biggest cost-saver in this project. The first Phase 3 dispatch skipped it and burned 5 CI cycles on format errors. Don't be that agent.

## 5. Test conventions

- Tag format: `[rules][<rule-id>]` for rule tests, `[util][<area>]` for utility tests, `[lsp]` / `[cfg]` / etc. for infra tests.
- ASCII-only test-case names. CTest chokes on Unicode. No em-dashes, only ASCII hyphens.
- Use the existing `lint_buffer()` / `has_rule()` helpers from `tests/unit/test_lerp_extremes.cpp`.
- Filter by `d.code == "<rule-id>"` only — never assert on total diagnostic count. The global registry fires every rule on every test snippet; cross-rule pollution is real.
- For `Stage::Reflection` tests: pass `LintOptions{.enable_reflection = true, .target_profile = "sm_6_6"}`.
- For `Stage::ControlFlow` tests: pass `LintOptions{.enable_control_flow = true}`.
- Tests should forward-declare the factory in `namespace shader_clippy::rules { make_<id>(); }` and instantiate the rule directly via `std::vector<std::unique_ptr<Rule>>`. This pattern lets tests compile before the wiring commit lands and keeps the test executable independent of `make_default_rules()`.
- Don't add a fixture-file test case for a new rule unless the corresponding fixture exists under `tests/fixtures/phaseN/`.

## 6. Don't-touch list (parallel-agent coordination)

When the orchestrator says "this runs in parallel with N other agents", you MUST NOT modify:

- `core/src/rules/rules.hpp` — registry forward declarations
- `core/src/rules/registry.cpp` — `make_default_rules()` body
- `core/CMakeLists.txt` — first-party source list
- `tests/CMakeLists.txt` — test executable source list (one specific exception: doc-only audits append their own test_*.cpp lines, never reorder existing)
- Top-level `CMakeLists.txt`
- `CLAUDE.md`, `ROADMAP.md`, any ADR file under `docs/decisions/`
- `THIRD_PARTY_LICENSES.md`
- `.github/workflows/*.yml`
- Anything under `external/`, `cmake/`, `lsp/`, `vscode-extension/` (unless your task is specifically there)
- Pre-existing rule `.cpp` / test `.cpp` files (unless your task is to fix one)

A separate wiring agent splices new rules into shared files after parallel packs return. Stay in your lane.

## 7. Reporting back to the orchestrator

End your task with a structured report:

1. Files created (full paths, grouped if many)
2. Factory function names verbatim (one line each, for the wiring agent)
3. Per-rule Stage choice (Ast / Reflection / ControlFlow)
4. Any rule that's a forward-compatible stub today (returns nothing pending a future engine extension) — flag with one-line reason
5. Any deviation from the task spec — flag with one-line reason
6. clang-format `--dry-run --Werror` confirmation: clean / not clean
7. Confirmation that the don't-touch list (§6) was respected

Keep the report tight. The orchestrator commits based on it; don't pad with prose the commit message will replicate.

## 8. References (read these only when needed for your specific task)

- `CLAUDE.md` — locked technical decisions, ban list, code-style enforcement. Most rule-pack agents do NOT need to read this; the conventions here in `AGENTS.md` cover the cross-task surface.
- `ROADMAP.md` — phase status, candidate-rule expansion section.
- `docs/decisions/0001`–`0014` — ADRs. Read the SPECIFIC ADR section your task references; don't read whole ADRs.
- `core/include/shader_clippy/*.hpp` — public API. Stable surface; don't modify.

---

Last updated: 2026-05-01 mid-Phase-4 (sub-phase 4c packs landing).
