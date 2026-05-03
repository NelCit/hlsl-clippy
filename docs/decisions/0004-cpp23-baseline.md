---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [code-standards, c++, build-system, phase-0]
---

# C++23 baseline + selective C++26 adoption

## Context and Problem Statement

The project currently sets `CMAKE_CXX_STANDARD 20` (`CMakeLists.txt`) and the `.clang-format` `Standard:` key is `c++20`. Several rule-engine ergonomics — fallible parse paths, structured diagnostics, no-CRTP visitors, sorted small-map registries — are markedly cleaner in C++23 than C++20.

In early 2026 the major toolchains have shipped enough C++23 to make a baseline bump cheap:

- MSVC 19.44+ (VS 17.14, May 2025) ships `std::expected`, `std::print`, deducing-this, `if consteval`, `[[assume]]`.
- Clang 18+ with libc++ 17+ or libstdc++ 13+ covers the same set.
- GCC 14+ with libstdc++ 14 covers it.

C++26 is starting to show up in the compilers but most of the headline features (static reflection, contracts, `std::execution`) are skeletal. A few small features (`std::inplace_vector`, pack indexing, `=delete("reason")`) are landing cleanly and worth picking up behind feature-test guards.

## Decision Drivers

- Make `core::Result<T> = std::expected<T, Diagnostic>` the canonical fallible-return shape. Banning exceptions across the `core` API boundary is a meaningful design constraint that needs `std::expected` to be ergonomic.
- Replace `std::puts`/`std::cout` formatting with `std::print` / `std::println`. Diagnostic rendering is a non-trivial format-string consumer.
- AST visitor base class without CRTP — deducing `this` is the canonical fix.
- Sorted small-map / small-set registries (rule registry, per-file suppression set) want `std::flat_map` / `std::flat_set`.
- Stay one major version behind the bleeding edge so MSVC LTSC + Ubuntu LTS users can build us. C++23 (not C++26) is that line in early 2026.

## Considered Options

1. **Stay on C++20.** No bump.
2. **C++23 baseline, defer C++26 entirely.** Adopt the `std::expected` + `std::print` + deducing-this + `flat_map` set; ignore P26 features.
3. **C++23 baseline + selective C++26 adoption.** Adopt `std::inplace_vector`, pack indexing, `=delete("reason")` behind feature-test macros; defer reflection / contracts / `std::execution`.
4. **C++26 baseline.** All-in.

## Decision Outcome

Chosen option: **(3) C++23 baseline + selective C++26 adoption.**

- `target_compile_features(<target> PRIVATE cxx_std_23)` per first-party target — **not** `set(CMAKE_CXX_STANDARD 23)` global, to avoid leaking C++23 into vendored Slang's C++17 expectations.
- C++23 wins to lean into:
  - `std::expected<T, Diagnostic>` as the canonical fallible-return type across rule and parser stages.
  - `std::print` / `std::println` for diagnostic rendering and CLI output.
  - Deducing `this` for AST visitor base classes (no CRTP).
  - `if consteval` for span/range utilities.
  - `[[assume]]` narrowly applied on hot loops.
  - `std::flat_map` / `std::flat_set` for small rule registries and per-file suppression sets.
- C++26 features adopted now, gated by `__cpp_lib_*` / `__cpp_*` feature-test macros:
  - `std::inplace_vector` (`__cpp_lib_inplace_vector`)
  - Pack indexing (P2662)
  - `=delete("reason")` (P2573)
- Deferred: static reflection (P2996), contracts (P2900), `std::execution` (P2300), hazard pointers / RCU.

Compiler floors:

- MSVC 19.44+ (VS 17.14).
- Clang 18+ (libc++ 17+ or libstdc++ 13+). Clang 19/20 strongly preferred. clang-tidy pinned to 19+ in CI.
- GCC 14+.

The CMakeLists.txt + .clang-tidy edits are tracked as a separate task — this ADR locks the policy; the build-system change comes after.

### Consequences

Good:

- Rule engine error paths gain `std::expected` (no exceptions across `core` API boundary).
- Diagnostic rendering gains `std::print` (single-allocation formatted output, type-checked at compile time).
- Visitor hierarchies drop a CRTP layer.
- Registry types are stack-allocated `flat_map` / `inplace_vector` where possible — heap pressure stays low.

Bad:

- MSVC < 19.44, Clang < 18, GCC < 13/14 stop building. Downstream packagers on older LTS images are excluded — acceptable for a 2026-launched tool.
- libc++ 17 / libstdc++ 13 floors — Ubuntu 22.04's stock libstdc++ (12) is below the floor. Pin to Ubuntu 24.04 in CI (already required by ADR 0005).
- Slang's C++17 build expectations + `_HAS_CXX23` macros: per-target `cxx_std_23` keeps Slang on its own standard. Mitigation: opaque-handle boundary across `libs/semantic/` (ADR 0003).
- MSVC 19.44 is the floor; the `std::print` console-encoding bug that affected 19.40 is resolved at this version. `/utf-8` remains set.
- **Validated against MSVC 19.50.35730 (VS 18 / 2026 Community)** as of 2026-05-01: CMake configure detects the compiler correctly (`-- The CXX compiler identification is MSVC 19.50.35730.0`), the Slang prebuilt cache loads, `cxx_std_23` resolves to `/std:c++latest`, and the full `core/`/`cli/`/`lsp/` build is clean. No 19.50-specific code changes needed beyond the existing `__cpp_lib_flat_map` feature-test guard added for cross-toolchain stdlib coverage. The 19.44 floor remains; 19.50 is just the most recent verified upper data point.

### Confirmation

- `target_compile_features(<target> PRIVATE cxx_std_23)` lands on every first-party target; vendored targets keep their own.
- CMake guards reject MSVC < 19.44, Clang < 18, GCC < 14:

```diff
 cmake_minimum_required(VERSION 3.20)
 project(shader-clippy LANGUAGES CXX VERSION 0.0.0)

-set(CMAKE_CXX_STANDARD 20)
-set(CMAKE_CXX_STANDARD_REQUIRED ON)
 set(CMAKE_CXX_EXTENSIONS OFF)
+# Per-target cxx_std_23 below (don't leak C++23 into vendored Slang/tree-sitter).

 set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

 if(MSVC)
     add_compile_options(
         /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /EHsc
+        /Zc:lambda /Zc:enumTypes /Zc:templateScope
     )
 endif()

 add_executable(shader-clippy src/main.cpp)
+target_compile_features(shader-clippy PRIVATE cxx_std_23)
+
+if(MSVC AND MSVC_VERSION LESS 1944)
+    message(FATAL_ERROR "MSVC 14.44 (VS 17.14) or newer required. Found MSVC ${MSVC_VERSION}.")
+elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
+    message(FATAL_ERROR "Clang 18+ required")
+elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
+    message(FATAL_ERROR "GCC 14+ required (libstdc++ <print>)")
+endif()
```

- `.clang-tidy` re-enables `cppcoreguidelines-pro-bounds-array-to-pointer-decay` and `cppcoreguidelines-pro-bounds-constant-array-index`; keeps `-pro-bounds-pointer-arithmetic` off only inside `core/parser/` via scoped `NOLINT`. Adds `modernize-use-std-print`, `bugprone-unchecked-optional-access`, `cppcoreguidelines-rvalue-reference-param-not-moved`, `cppcoreguidelines-avoid-const-or-ref-data-members`, etc.
- `.clang-format` `Standard:` bumped from `c++20` to `c++23`.
- Project ban list documented in `ROADMAP.md` "Code standards":
  - No exceptions across the `core` API boundary (use `std::expected`).
  - No `std::endl` in hot paths.
  - No `using namespace` at file scope.
  - No C-style casts; no raw owning pointers; no implicit narrowing; no `goto`.

## Pros and Cons of the Options

### Stay on C++20

- Good: zero migration cost.
- Bad: error paths stay exception-shaped or hand-rolled; visitor patterns stay CRTP-shaped; no `std::print`. Every rule pays the tax.

### C++23 baseline, defer C++26 entirely

- Good: simple, safe, well-supported floors.
- Bad: leaves cheap C++26 wins on the table — `std::inplace_vector` would otherwise be hand-rolled, pack indexing would otherwise be `std::tuple_element_t`-soup.

### C++23 baseline + selective C++26 (chosen)

- Good: cheap C++26 wins, gated by feature-test macros so old toolchains still build.
- Good: stays one major-version behind C++26's full surface — no exposure to immature reflection / contracts implementations.
- Bad: feature-test guards add small `#ifdef` cost in a few places.

### C++26 baseline

- Good: maximum modern feature surface.
- Bad: reflection / contracts / `std::execution` are skeletal in early 2026; relying on them means tying the project to a single compiler fork (Clang for reflection).
- Bad: pushes compiler floors further; effectively excludes anyone on a release older than a few months.

## Links

- Verbatim research: `_research/cpp23-uplift.md` §1-5.
- Project memory: `feedback_cpp_standards.md` (Core Guidelines + /W4 /WX policy — preserved; this ADR extends it).
- Related: ADR 0003 (per-target `cxx_std_23` interacts with module isolation), ADR 0005 (CI compiler matrix enforces the floors).
