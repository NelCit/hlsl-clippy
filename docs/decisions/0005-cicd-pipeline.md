---
status: Accepted
date: 2026-04-30
deciders: NelCit
tags: [ci-cd, devops, build-system, phase-0]
---

# CI/CD pipeline

## Context and Problem Statement

Phase 0 needs CI green on Windows (MSVC) + Linux (Clang) before any rule lands. The pipeline must build the Slang submodule from source — not a five-minute job — without making every PR cycle 25 minutes long. CI is also the place where `/W4 /WX`, clang-tidy with `WarningsAsErrors: '*'`, clang-format, and per-rule fixture tests get enforced.

We need to decide: which runners, which compilers and pinned versions, what's cached and how, what gates a PR, what the Phase-0 `CMakeLists.txt` interface library shape is, and what the release-binary story looks like.

## Decision Drivers

- Two-OS matrix from day one (Windows + Linux). macOS deferred per existing ROADMAP open question.
- Cold Slang build is 20+ min — caching is mandatory, not optional.
- C++23 floors (ADR 0004) dictate compiler pins (MSVC 19.40+, Clang 18+, GCC 14+).
- Warnings-as-errors leaks into vendored deps unless we scope warning flags to first-party targets only.
- Single-binary distribution per OS is a stated ROADMAP goal.

## Considered Options

The decision space is multi-axis; for each axis the chosen option is bolded.

- Runners: **windows-2022 + ubuntu-24.04** vs (older / mixed images).
- Compilers on Linux: stock GCC vs **Clang 18 from `apt.llvm.org`**.
- Compiler-launcher: ccache vs **sccache** (only sccache has credible MSVC support).
- Slang vendoring: FetchContent vs CPM vs **git submodule with pinned SHA**.
- Cache layers: single monolithic vs **three independent (Slang install prefix, sccache, CMake configure)**.
- Test framework: GoogleTest vs doctest vs **Catch2 v3**.
- Distribution: CPack-installer vs **`actions/upload-artifact` per matrix leg → tag-triggered `softprops/action-gh-release@v2`**.

## Decision Outcome

### Runners + toolchains

- **`windows-2022`** — MSVC 19.40+ (VS 2022 17.10), bundled with the image. Pin via `microsoft/setup-msbuild` only if a specific BuildTools side-load becomes necessary; default image is fine.
- **`ubuntu-24.04`** — Clang 18 from `apt.llvm.org`. Install `clang-18 clang-tidy-18 clang-format-18 lld-18 libc++-18-dev`. Don't trust the image's default Clang — it lags. clang-tidy pinned to 19+ once it's in the apt channel (ADR 0004 requires it for `modernize-use-std-print`).
- macOS deferred until Phase 5 (existing ROADMAP open question).

### Caching — three independent layers, ordered by churn

| Cache | Tool | Key |
| --- | --- | --- |
| Slang submodule build + install prefix | `actions/cache` over `build/_deps/slang-build/` and the install prefix | `slang-${{ runner.os }}-${{ hashFiles('.gitmodules', 'third_party/slang-rev.txt') }}-${{ env.MSVC_VER \|\| env.CLANG_VER }}` |
| Compiler object cache | sccache (`CMAKE_<LANG>_COMPILER_LAUNCHER=sccache`) | `sccache-${{ runner.os }}-${{ github.ref }}` with restore-keys `sccache-${{ runner.os }}-` |
| CMake configure cache | `actions/cache` over `build/CMakeCache.txt` and `build/CMakeFiles/` | hashed on `**/CMakeLists.txt` + `CMakePresets.json` |

Slang specifically: separate `cmake --install` step into `third_party/slang-install/` and cache that prefix. Cold build is 20+ min; restore is ~30s. Submodule SHA bump invalidates the cache by design.

sccache, not ccache: ccache fights MSVC. sccache is the only cache with credible MSVC support and works equally well on Clang.

### CMakePresets.json structure

`dev-debug`, `dev-release`, `dev-asan`, `ci-msvc`, `ci-clang`. `HLSLC_SANITIZE` is a project-defined option surfaced as `target_compile_options` on the warnings interface library.

### CI gates (hard fails)

- Any `/W4 /WX` (MSVC) or `-Werror` (Clang) warning.
- Any clang-tidy diagnostic (`WarningsAsErrors: '*'` is already in `.clang-tidy`).
- `clang-format --dry-run --Werror` on tracked sources.
- Any ctest failure.
- Coverage below threshold. **Starting threshold: 60% line coverage on `core/`** via llvm-cov on the Clang job. Bump to 75% by Phase 2, 80% by Phase 4. Coverage enforced only on the Clang job — MSVC-side coverage is annotation-only.

Soft (annotate but don't fail at Phase 0): Doxygen warnings, IWYU.

### CMakeLists.txt review

Current root `CMakeLists.txt` is fine for Phase 0 stub but won't survive Phase 1:

- `add_compile_options` is global — leaks `/WX` into vendored Slang and tree-sitter, which warning-spam our build.
- No `/Zc:throwingNew /Zc:inline`.
- `-Wconversion -Wsign-conversion` at the root will scream when Slang's headers are included transitively.
- No `option()` knobs (`HLSLC_BUILD_TESTS`, `HLSLC_SANITIZE`, `HLSLC_USE_VENDORED_SLANG`).
- No `enable_testing()` / `include(CTest)`.

Proposed shape (to land alongside the rename — this ADR doesn't itself edit `CMakeLists.txt`):

- `/CMakeLists.txt` — `project()`, C++ settings, options, `add_subdirectory(third_party)`, `add_subdirectory(core)`, `add_subdirectory(cli)`, `add_subdirectory(tests)`. Defines `hlslc_warnings` INTERFACE library carrying `/W4 /WX /permissive-` via `target_compile_options(... $<COMPILE_LANG_AND_ID:CXX,MSVC>:...>)`. Also `hlslc_sanitizers` interface gated on `HLSLC_SANITIZE`.
- `/core/CMakeLists.txt` — `add_library(hlslc_core STATIC ...)`, `target_include_directories PUBLIC include PRIVATE src`, `target_link_libraries hlslc_core PUBLIC slang tree-sitter PRIVATE hlslc_warnings`.
- `/cli/CMakeLists.txt` — `add_executable(hlsl-clippy src/main.cpp)`, `target_link_libraries hlsl-clippy PRIVATE hlslc_core hlslc_warnings`.

**Rule: warning flags live on an INTERFACE lib that ONLY first-party targets link to. Vendored libs never see `/WX`.**

### Pre-commit + dev tooling

Phase 0 minimal: `clang-format`, `end-of-file-fixer`, `trailing-whitespace`, `check-yaml`, `mixed-line-ending`, `conventional-commits` commit-msg hook. Skip clang-tidy locally — it needs `compile_commands.json` and is slow; CI carries it.

Conventional Commits v1.0.0 (`feat:`, `fix:`, `refactor:`, `docs:`, `chore:`, `test:`, `ci:`). Useful later for auto-changelog via `git-cliff` at v0.5 launch.

### Test framework

**Catch2 v3**, `FetchContent_Declare`'d. Header-stable, modern macros, no GoogleTest's death-test threading complications.

### Releases

CPack is overkill for one binary per OS. Use:

1. `actions/upload-artifact` on each matrix leg.
2. Tag-triggered job downloads artifacts and runs `softprops/action-gh-release@v2`.

- **Windows**: `hlsl-clippy.exe` + `slang.dll` if Slang is dynamic. `/DEBUG:NONE` on Release configs but emit a separate `.pdb` uploaded as a debug artifact.
- **Linux**: `hlsl-clippy` static-linked against libstdc++ via `-static-libstdc++ -static-libgcc`; `strip --strip-unneeded` post-build. Don't statically link glibc — build on the oldest supported Ubuntu (24.04 baseline; revisit `manylinux_2_28` before v0.1).
- **Slang**: prefer static. Phase 0 may ship Slang as shared library next to binary; revisit static-link at Phase 6.

### Consequences

Good:

- Cold-cache CI ~25 min; warm-cache CI ~3-5 min. Enables a tight rule-development loop.
- Vendored deps stop warning-spam'ing on `/WX` because warnings live on an INTERFACE-lib boundary.
- Three independent caches mean a Slang bump doesn't blow out the sccache and vice versa.
- Single-binary release path; one artifact upload step per OS.

Bad:

- `apt.llvm.org` install adds ~30s to every Linux job.
- Submodule SHA bumps trigger a full Slang rebuild (~20 min). Acceptable — bumps should be deliberate.
- macOS coverage gap until Phase 5.
- We own the warnings INTERFACE library and the sanitizer toggles — minor CMake bookkeeping.

### Confirmation

- `.github/workflows/` defines `windows-2022` + `ubuntu-24.04` matrix.
- `CMakePresets.json` defines the five presets above.
- `cmake --install` step lands Slang into `third_party/slang-install/` and that prefix is `actions/cache`'d.
- `hlslc_warnings` INTERFACE library carries `/W4 /WX /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic -Werror` (Clang/GCC) and is linked **only** by first-party targets.
- ctest invocation runs Catch2 binaries with `--reporter junit` for GitHub Actions annotations.

## Open questions resolved by this ADR

(Cross-referenced from devops research §7.)

1. **Slang vendoring**: git submodule with manually-bumped `.gitmodules` SHA pin.
2. **Linux distro floor**: ubuntu-24.04 (glibc 2.39, March 2024). Decide on `manylinux_2_28` build container before v0.1.
3. **libc++ vs libstdc++ on Clang**: libc++-18 in CI; revisit if real-world consumers need libstdc++.
4. **Test framework**: Catch2 v3.
5. **GSL source**: FetchContent (header-only).
6. **Coverage tool on MSVC**: not enforced; Clang job carries the coverage gate.
7. **Windows ARM64**: punt.
8. **Tree-sitter C runtime**: in-tree wrapper (~20 lines) over upstream CMake.

## Pros and Cons of the Options

### Single GHA matrix vs split workflows

- Good: matrix keeps Windows + Linux building from the same workflow file; cancel-on-fail keeps queue cost down.
- Bad: matrix `include:` clauses can grow gnarly when the OSes need different toolchain steps.
- Verdict: matrix is fine for two OSes; revisit if macOS adds a third.

### sccache vs ccache

- sccache: works on MSVC + Clang, supports cloud backends if we ever want one, single binary per OS.
- ccache: more mature on Linux, broken on MSVC.
- Verdict: sccache. The MSVC support is the deciding factor.

### Slang submodule vs FetchContent vs CPM

- Submodule: reviewable patches, frozen SHA, CI-cache-friendly. Slang's own submodules (glslang, spirv-tools) are a real factor — recursive submodule fetch is bounded and known.
- FetchContent: tempting; one less command. But we'd lose patch reviewability and the shallow-clone behavior interacts poorly with Slang's own submodules.
- CPM: nicer ergonomics on top of FetchContent; same shallow-clone problem.
- Verdict: submodule.

### Catch2 v3 vs GoogleTest vs doctest

- Catch2 v3: header-stable, modern, easy generator-based parameterized tests. `BENCHMARK` macro useful for the future fixture-perf tests.
- GoogleTest: industry default, but death-test threading interacts badly with Slang's globals.
- doctest: lightest, but generator/property-based feature set thinner.
- Verdict: Catch2 v3.

## Links

- Verbatim research: `_research/devops-plan.md` §1-7.
- Related: ADR 0001 (Slang vendoring), ADR 0003 (CMake target structure), ADR 0004 (compiler floors).
