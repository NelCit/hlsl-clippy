<!--
date: 2026-04-30
prompt-summary: review the C++ standard floor for shader-clippy and propose a C++23 uplift with selective C++26 adoption — Core Guidelines tightening, GSL adoption policy, project ban list, and concrete CMakeLists.txt + .clang-tidy diff hunks.
preserved-verbatim: yes — see ../0004-cpp23-baseline.md for the distilled decision.
-->

# shader-clippy: C++23 Upgrade & Core Guidelines Tightening Plan

## 1. Standard version baseline

Recommendation: pin C++23 as project baseline. Compiler floors (early 2026):
- MSVC 19.40+ (VS 17.10, May 2024) — required for std::expected, std::print, deducing-this, if consteval, [[assume]]. 19.42+ recommended (cleaner <print>, stable std::flat_map).
- Clang 18+ with libc++ 17+ or libstdc++ 13+. Clang 19/20 strongly preferred.
- GCC 14+ with libstdc++ 14.

C++26 today (early 2026):
- Static reflection (P2996) — defer (Clang fork only).
- Contracts (P2900) — defer (skeletal implementations).
- std::execution (P2300) — defer for the linter core.
- Hazard pointers / RCU — skip.
- std::inplace_vector (P0843) — adopt-now behind __cpp_lib_inplace_vector guard.
- Pack indexing (P2662) and =delete("reason") (P2573) — cheap wins gate with feature-test macros.

target_compile_features(... PRIVATE cxx_std_23) is preferred over set(CMAKE_CXX_STANDARD 23) — per-target form prevents leaking C++23 into Slang's C++17 expectations.

## 2. Concrete C++23 wins for THIS project

- std::expected<T, Diagnostic> — replace exception-based error paths in rule engine and parser bridge. Make core::Result<T> = std::expected<T, Diagnostic> the canonical return type. Ban exceptions across the core API boundary.
- std::print / std::println — replace std::puts in src/main.cpp.
- Deducing this — AST visitor base class without CRTP.
- if consteval — span/range utility code.
- [[assume]] — narrowly applied on hot loops.
- std::flat_map / std::flat_set — small rule registries, per-file suppression set.
- Multidimensional operator[] — not relevant; skip.
- import std; — defer; CMake module support is finicky with mixed-stdlib + clang-tidy 19.

## 3. Tightened Core Guidelines enforcement

Re-enable from current .clang-tidy disables:
- cppcoreguidelines-pro-bounds-array-to-pointer-decay — turn back on.
- cppcoreguidelines-pro-bounds-constant-array-index — turn on.
- Keep pro-bounds-pointer-arithmetic disabled only in core/parser/ (NOLINT scoped).

Critical cppcoreguidelines-* to add explicitly:
init-variables, narrowing-conversions, prefer-member-initializer, slicing, virtual-class-destructor, rvalue-reference-param-not-moved, pro-type-cstyle-cast, pro-type-member-init, special-member-functions, avoid-const-or-ref-data-members.

bugprone-* keep list: unchecked-optional-access, dangling-handle, use-after-move, assignment-in-if-condition.

modernize-* worth promoting: modernize-use-std-print, modernize-use-constraints, modernize-type-traits, modernize-use-designated-initializers, modernize-use-nodiscard.

GSL adoption policy:
- Use: gsl::span<const std::byte> for source-buffer views; gsl::not_null<Rule*> at registry boundaries; gsl::narrow_cast where narrowing is intentional/audited; Expects/Ensures in core/ public functions.
- Don't use: gsl::owner — we have unique_ptr. gsl::final_action — RAII clearer. gsl::string_span — std::string_view.

Project ban list: no raw owning pointers, no C-style casts, no goto, no implicit narrowing, no exceptions across core API boundary (use std::expected), no using namespace at file scope, no std::endl in hot paths.

## 4. CMakeLists.txt upgrade — proposed diff

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
+if(MSVC AND MSVC_VERSION LESS 1940)
+    message(FATAL_ERROR "MSVC 19.40+ required for C++23 std::expected/std::print")
+elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
+    message(FATAL_ERROR "Clang 18+ required")
+elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
+    message(FATAL_ERROR "GCC 14+ required (libstdc++ <print>)")
+endif()
```

MSVC: cxx_std_23 resolves to /std:c++latest on 19.40 — that's correct.

## 5. .clang-tidy upgrade — proposed diff

(Re-enable the three disabled pro-bounds-* checks except parser-specific scope; explicitly pin the modernize-use-std-print and bugprone-unchecked-optional-access checks.)

## 6. Risks

- Slang and tree-sitter (C/C++17): linking is supported on all three toolchains. Concern: if Slang ships its own bundled <expected> shim or uses _HAS_CXX23 in headers, mixed-config builds can disagree. Mitigation: keep Slang in its own static lib target with its own cxx_std_17 (per-target), opaque handles across the boundary.
- std::expected stdlib version: libstdc++ 13+ ✓, libc++ 17+ ✓, MSVC STL 19.37+ ✓ — all pinned floors clear.
- clang-tidy 18 vs 19: modernize-use-std-print and improved deducing-this lands in tidy 19. Pin clang-tidy 19 in CI.
- std::print Unicode on Windows: MSVC 19.40 had a console-encoding bug. /utf-8 is set; verify by Phase 1.
