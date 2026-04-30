# Slang vendor pin

## Tag

`v2026.7.1` — latest stable release as of 2026-04-30.

GitHub release page: <https://github.com/shader-slang/slang/releases/tag/v2026.7.1>

## Build flags set in `cmake/UseSlang.cmake`

| Flag | Value | Reason |
|------|-------|--------|
| `SLANG_ENABLE_TESTS` | `OFF` | Saves many minutes of compile time and avoids heavy test deps |
| `SLANG_ENABLE_EXAMPLES` | `OFF` | Examples require `slang-rhi` / Vulkan / DX12 etc. |
| `SLANG_ENABLE_GFX` | `OFF` | Graphics layer not needed for CLI tooling |
| `SLANG_ENABLE_SLANGD` | `OFF` | Language server not needed |
| `SLANG_ENABLE_SLANGRT` | `OFF` | Runtime test harness not needed |
| `SLANG_ENABLE_REPLAYER` | `OFF` | Replay tool not needed |
| `SLANG_ENABLE_SLANGI` | `OFF` | Interpreter not needed |

These flags reduce the build to only the compiler library (`slang` / `slang-compiler`)
and its mandatory dependencies (miniz, lz4, unordered_dense, glslang, spirv-headers,
spirv-tools).

## Slang-specific build prerequisites

| Prerequisite | Version | Notes |
|---|---|---|
| CMake | ≥ 3.22 | Slang's `CMakeLists.txt` sets `cmake_minimum_required(VERSION 3.22)` |
| C++ compiler | MSVC 19.3+, Clang 13+, or GCC 11+ | C++17 required by Slang itself |
| Python | ≥ 3.x | Required by Slang's build scripts for embedding the core module |
| Ninja (recommended) | any | `cmake -G Ninja` gives much faster incremental builds |

### Windows notes

- Slang ships a `WindowsToolchain` submodule (MSVC runtime re-distribution); it is
  checked out by `git submodule update --init --recursive`.
- On Windows the library is built as a DLL (`slang-compiler.dll`) plus a thin
  forwarding proxy (`slang.dll` for backwards compatibility).  Both must be on
  `PATH` (or next to the executable) at runtime.
- The first configure is slow because Slang embeds the core Slang standard library
  as a binary blob; subsequent incremental builds are fast.

### Full-build size warning

Slang's *full* build (with tests, examples, GFX, LLVM back-end, etc.) can consume
multiple gigabytes of disk space and take 30+ minutes on a single-core machine.
With the flags above the partial build is substantially smaller (~200–400 MB object
files + output DLLs), but still non-trivial.  A pre-built binary release is an
alternative if build time is a concern; see
<https://github.com/shader-slang/slang/releases/tag/v2026.7.1> for release artifacts.
