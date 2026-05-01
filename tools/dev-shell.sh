#!/usr/bin/env bash
#
# tools/dev-shell.sh
#
# POSIX (Linux + macOS) equivalent of tools/dev-shell.ps1. Source this
# script (don't execute it) to set up a contributor's shell for
# building hlsl-clippy:
#
#     . tools/dev-shell.sh
#
# Re-running is idempotent (HLSL_CLIPPY_DEV_SHELL_READY guard); set
# HLSL_CLIPPY_DEV_SHELL_FORCE=1 to force a reinit.
#
# What it does:
#   1. Sanity-check the toolchain (clang-18 / clang++-18 on Linux,
#      brew's llvm@18 on macOS).
#   2. Populate the Slang prebuilt cache via tools/fetch-slang.sh if
#      the cache is empty.
#   3. Export Slang_ROOT for cmake/UseSlang.cmake to skip the
#      walk-up search.
#   4. Print a one-line "ready" banner with what's wired.

# This file is intended to be `.`-sourced; running it directly is fine
# but only the env vars set inside the function survive.

# --- Idempotency guard -----------------------------------------------------
if [[ "${HLSL_CLIPPY_DEV_SHELL_READY:-}" == "1" && "${HLSL_CLIPPY_DEV_SHELL_FORCE:-}" != "1" ]]; then
    echo "dev-shell: already configured (set HLSL_CLIPPY_DEV_SHELL_FORCE=1 to redo)"
    return 0 2>/dev/null || exit 0
fi

# --- Locate repo root ------------------------------------------------------
# Resolve relative to this script even when sourced from elsewhere.
_dev_shell_script="${BASH_SOURCE[0]:-$0}"
_dev_shell_dir="$(cd "$(dirname "$_dev_shell_script")/.." && pwd)"

if [[ ! -f "$_dev_shell_dir/cmake/SlangVersion.cmake" ]]; then
    echo "dev-shell: ERROR — could not find repo root (cmake/SlangVersion.cmake missing)" >&2
    return 1 2>/dev/null || exit 1
fi

# --- Detect OS + toolchain -------------------------------------------------
_uname_s="$(uname -s)"
case "$_uname_s" in
    Linux*)
        _os="Linux"
        _expected_cxx="clang++-18"
        _expected_cc="clang-18"
        ;;
    Darwin*)
        _os="macOS"
        _llvm_prefix="$(brew --prefix llvm@18 2>/dev/null || echo '/opt/homebrew/opt/llvm@18')"
        _expected_cxx="$_llvm_prefix/bin/clang++"
        _expected_cc="$_llvm_prefix/bin/clang"
        ;;
    *)
        echo "dev-shell: WARN — unrecognised OS '$_uname_s'; falling back to system clang++" >&2
        _os="$_uname_s"
        _expected_cxx="clang++"
        _expected_cc="clang"
        ;;
esac

if ! command -v "$_expected_cxx" >/dev/null 2>&1 && [[ ! -x "$_expected_cxx" ]]; then
    echo "dev-shell: ERROR — $_expected_cxx not found." >&2
    if [[ "$_os" == "Linux" ]]; then
        echo "dev-shell: install via 'wget -qO- https://apt.llvm.org/llvm.sh | sudo bash -s -- 18 all'" >&2
        echo "dev-shell: then 'sudo apt-get install -y libc++-18-dev libc++abi-18-dev'" >&2
    elif [[ "$_os" == "macOS" ]]; then
        echo "dev-shell: install via 'brew install llvm@18' (Apple Clang is too old for C++23)" >&2
    fi
    return 1 2>/dev/null || exit 1
fi

# --- macOS PATH wiring -----------------------------------------------------
# brew's llvm@18 is keg-only — prepend its bin/ so the unversioned
# `clang++` resolves to the keg version.
if [[ "$_os" == "macOS" && -d "$_llvm_prefix/bin" ]]; then
    case ":${PATH}:" in
        *":$_llvm_prefix/bin:"*) ;;
        *) export PATH="$_llvm_prefix/bin:$PATH" ;;
    esac
fi

# --- Slang prebuilt cache --------------------------------------------------
_slang_version="$(grep -E '^set\(HLSL_CLIPPY_SLANG_VERSION ' "$_dev_shell_dir/cmake/SlangVersion.cmake" \
    | sed -E 's/.*"([^"]+)".*/\1/')"
_slang_cache_root="${HLSL_CLIPPY_SLANG_CACHE:-$HOME/.cache/hlsl-clippy/slang}"
_slang_cache_dir="$_slang_cache_root/$_slang_version"

if [[ ! -d "$_slang_cache_dir/include" || ! -e "$_slang_cache_dir/include/slang.h" ]]; then
    echo "dev-shell: Slang $_slang_version cache empty; populating via tools/fetch-slang.sh"
    if ! bash "$_dev_shell_dir/tools/fetch-slang.sh"; then
        echo "dev-shell: ERROR — fetch-slang.sh failed; you may need to run it manually" >&2
        return 1 2>/dev/null || exit 1
    fi
fi

# Export Slang_ROOT so cmake/UseSlang.cmake hits the cache directly.
export Slang_ROOT="$_slang_cache_dir"

# --- Done ------------------------------------------------------------------
export HLSL_CLIPPY_DEV_SHELL_READY=1

echo "dev-shell: $_os toolchain wired"
echo "dev-shell:   CC=$_expected_cc"
echo "dev-shell:   CXX=$_expected_cxx"
echo "dev-shell:   Slang_ROOT=$Slang_ROOT"
echo "dev-shell: ready. Now run cmake / ninja / ctest directly."

unset _dev_shell_script _dev_shell_dir _uname_s _os _expected_cxx _expected_cc _llvm_prefix _slang_version _slang_cache_root _slang_cache_dir
