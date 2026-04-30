<!--
date: 2026-04-30
prompt-summary: produce a CI/CD + build plan for hlsl-clippy Phase 0 — runners, compilers, caching, gates, CMake hygiene, dev tooling, releases, and remaining open questions.
preserved-verbatim: yes — see ../0005-cicd-pipeline.md for the distilled decision.
-->

# hlsl-clippy CI/CD + Build Plan

## 1. GitHub Actions matrix

Runners and toolchains:
- `windows-2022` — MSVC 19.40+ (VS 2022 17.10), bundled with the image. Pin via `microsoft/setup-msbuild` only if you need a specific BuildTools side-load; default image is fine.
- `ubuntu-24.04` — Clang 18 from `apt.llvm.org` (`bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)" -- 18`). Don't trust the image's default; it lags. Install `clang-18 clang-tidy-18 clang-format-18 lld-18 libc++-18-dev`.
- macOS deferred (Phase 5 per ROADMAP open-questions section).

Caching strategy — three independent caches, ordered by churn:
| Cache | Tool | Key |
| Slang submodule build | actions/cache over build/_deps/slang-build and the install prefix | slang-${{ runner.os }}-${{ hashFiles('.gitmodules', 'third_party/slang-rev.txt') }}-${{ env.MSVC_VER || env.CLANG_VER }} |
| Compiler object cache | sccache (works on both MSVC and Clang; ccache fights MSVC) | sccache-${{ runner.os }}-${{ github.ref }} w/ restore-keys: sccache-${{ runner.os }}- |
| CMake configure cache | actions/cache over build/CMakeCache.txt, build/CMakeFiles/ | keyed on hashFiles('**/CMakeLists.txt', 'CMakePresets.json') |

Why sccache over ccache: sccache is the only one with credible MSVC support (CMAKE_<LANG>_COMPILER_LAUNCHER=sccache). For Slang specifically, do a separate cmake --install step into third_party/slang-install/ and cache that prefix.

Cold Slang build is 20+ min. After the cache lands, restore is ~30s.

## 2. CMakePresets.json structure

Recommend: configure presets `dev-debug`, `dev-release`, `dev-asan`, `ci-msvc`, `ci-clang`. `HLSLC_SANITIZE` is a project-defined option surfaced as `target_compile_options` on the warnings interface lib (see §4).

## 3. CI gates

Hard fails:
- Any /W4 /WX (MSVC) or -Werror (Clang) warning.
- Any clang-tidy diagnostic — already enforced via WarningsAsErrors: '*' in .clang-tidy.
- clang-format --dry-run --Werror on tracked sources.
- Any ctest failure.
- Coverage below threshold. Starting threshold: **60% line coverage on core/** via llvm-cov on the Clang job. Bump to 75% by Phase 2, 80% by Phase 4.

Soft (annotate but don't fail at Phase 0): Doxygen warnings, IWYU.

## 4. CMakeLists.txt review

Current root file is fine for Phase 0 stub but won't survive Phase 1.
- add_compile_options is global — leaks /WX into vendored Slang and tree-sitter, which warning-spam your build. Move flags to an INTERFACE target.
- No /Zc:throwingNew /Zc:inline.
- -Wconversion -Wsign-conversion at the root will scream when Slang's headers are included transitively. Keep them but only on first-party targets.
- No option() knobs (HLSLC_BUILD_TESTS, HLSLC_SANITIZE, HLSLC_USE_VENDORED_SLANG).
- No enable_testing() / include(CTest).

Proposed split (after rename):
- /CMakeLists.txt — project(), C++ settings, options, add_subdirectory(third_party), add_subdirectory(core), add_subdirectory(cli), add_subdirectory(tests). Defines hlslc_warnings INTERFACE library carrying /W4 /WX /permissive- via target_compile_options (...COMPILE_LANG_AND_ID:CXX,MSVC... ). Also hlslc_sanitizers interface gated on HLSLC_SANITIZE.
- /core/CMakeLists.txt — add_library(hlslc_core STATIC ...), target_include_directories PUBLIC include PRIVATE src, target_link_libraries hlslc_core PUBLIC slang tree-sitter PRIVATE hlslc_warnings.
- /cli/CMakeLists.txt — add_executable(hlsl-clippy src/main.cpp), target_link_libraries hlsl-clippy PRIVATE hlslc_core hlslc_warnings.

Rule: warnings flags on an INTERFACE lib that ONLY first-party targets link to. Vendored libs never see /WX.

## 5. Pre-commit + dev tooling

Recommend minimal pre-commit at Phase 0: clang-format, end-of-file-fixer, trailing-whitespace, check-yaml, mixed-line-ending, conventional-commits commit-msg. Skip clang-tidy locally — it needs compile_commands.json and is slow; let CI carry it.

Conventional Commits spec: v1.0.0 (feat:, fix:, refactor:, docs:, chore:, test:, ci:). Useful later for auto-changelog via git-cliff at v0.5 launch.

## 6. Releases

CPack is overkill for one binary per OS. Use actions/upload-artifact on each matrix leg → tag-triggered job downloading artifacts and running softprops/action-gh-release@v2.

- Windows: hlsl-clippy.exe plus slang.dll if dynamic. Strip via /DEBUG:NONE on Release configs but emit a separate .pdb uploaded as a debug artifact.
- Linux: hlsl-clippy static-linked against libstdc++ via -static-libstdc++ -static-libgcc; strip --strip-unneeded post-build. Don't statically link glibc — build on the oldest supported Ubuntu.
- Slang: prefer static. Phase 0 may ship Slang as shared library next to binary; revisit static-link at Phase 6.

## 7. Open questions

1. Slang vendoring mechanism — submodule vs FetchContent vs CPM.cmake? Recommend submodule with manually-bumped .gitmodules SHA pin.
2. Linux distro floor — ubuntu-24.04 gives glibc 2.39 (Mar 2024). Decide before v0.1 whether to add manylinux_2_28 build container.
3. libc++ vs libstdc++ on Clang.
4. Test framework — Catch2 v3 vs GoogleTest vs doctest? Recommend Catch2 v3.
5. GSL source — vendored or FetchContent?
6. Coverage tool on MSVC — only enforce coverage on Clang job.
7. Windows ARM64 — punt.
8. Tree-sitter C runtime — in-tree wrapper (~20 lines) over upstream CMake.
