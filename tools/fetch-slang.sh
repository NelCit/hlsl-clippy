#!/usr/bin/env bash
#
# tools/fetch-slang.sh
#
# POSIX (Linux + macOS) equivalent of tools/fetch-slang.ps1. Populates the
# per-user prebuilt Slang cache so fresh clones / new git worktrees do not
# have to rebuild Slang from source (~20 minutes cold).
#
# What it does
#   1. Reads the pinned Slang version from cmake/SlangVersion.cmake
#      (regex-parsed; CMake is NOT invoked).
#   2. Detects host OS + architecture:
#        Linux  + x86_64  → linux-x86_64
#        Linux  + aarch64 → linux-aarch64 (best-effort; upstream availability)
#        Darwin + arm64   → macos-aarch64
#        Darwin + x86_64  → macos-x86_64
#      Override the resolved triple with `--triple <value>` when needed.
#   3. Computes the cache target dir:
#        $HOME/.cache/shader-clippy/slang/<version>/
#      (or $SHADER_CLIPPY_SLANG_CACHE/<version>/ if that env var is set)
#      The cache root is shared across Linux + macOS for cross-platform
#      consistency with cmake/UseSlang.cmake's resolver.
#   4. If that dir already has a non-empty include/ subdir, exits 0.
#   5. Otherwise downloads
#        https://github.com/shader-slang/slang/releases/download/v<version>/slang-<version>-<triple>.tar.gz
#      to a temp file, untars it into the cache dir, and cleans up.
#
# Usage (from a fresh clone, repo root):
#   bash tools/fetch-slang.sh                            # idempotent
#   bash tools/fetch-slang.sh --force                    # wipe + redownload
#   bash tools/fetch-slang.sh --version 2026.7.1         # override pinned version
#   bash tools/fetch-slang.sh --triple macos-aarch64     # override host triple
#
# Requires: bash, curl OR wget, tar, sed/grep, uname. No CMake invocation.

set -euo pipefail

# --- Arg parsing -----------------------------------------------------------
FORCE=0
VERSION=""
TRIPLE_OVERRIDE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -f|--force)
            FORCE=1
            shift
            ;;
        -v|--version)
            VERSION="${2:-}"
            if [[ -z "$VERSION" ]]; then
                echo "fetch-slang: --version requires an argument" >&2
                exit 1
            fi
            shift 2
            ;;
        -t|--triple)
            TRIPLE_OVERRIDE="${2:-}"
            if [[ -z "$TRIPLE_OVERRIDE" ]]; then
                echo "fetch-slang: --triple requires an argument" >&2
                exit 1
            fi
            shift 2
            ;;
        -h|--help)
            sed -n '3,36p' "$0"
            exit 0
            ;;
        *)
            echo "fetch-slang: unknown argument '$1'" >&2
            exit 1
            ;;
    esac
done

# --- Locate the repo root --------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION_FILE="$REPO_ROOT/cmake/SlangVersion.cmake"

# --- Resolve pinned version ------------------------------------------------
if [[ -z "$VERSION" ]]; then
    if [[ ! -f "$VERSION_FILE" ]]; then
        echo "fetch-slang: cannot find $VERSION_FILE. Are you running this from a clone of shader-clippy?" >&2
        exit 1
    fi
    # Match: set(SHADER_CLIPPY_SLANG_VERSION "X.Y.Z" ...)
    VERSION="$(grep -oE 'SHADER_CLIPPY_SLANG_VERSION[[:space:]]+"[^"]+"' "$VERSION_FILE" \
        | head -n1 \
        | sed -E 's/.*"([^"]+)".*/\1/')"
    if [[ -z "$VERSION" ]]; then
        echo "fetch-slang: failed to parse SHADER_CLIPPY_SLANG_VERSION from $VERSION_FILE" >&2
        exit 1
    fi
fi

echo "fetch-slang: target Slang version = $VERSION"

# --- Resolve host triple ----------------------------------------------------
# Map (uname -s, uname -m) → Slang release-asset triple. Override with
# --triple to force a specific value (cross-population from a build host
# with a different OS/arch).
if [[ -n "$TRIPLE_OVERRIDE" ]]; then
    TRIPLE="$TRIPLE_OVERRIDE"
else
    UNAME_S="$(uname -s 2>/dev/null || echo unknown)"
    UNAME_M="$(uname -m 2>/dev/null || echo unknown)"
    case "$UNAME_S" in
        Linux)
            case "$UNAME_M" in
                x86_64|amd64)  TRIPLE="linux-x86_64" ;;
                aarch64|arm64) TRIPLE="linux-aarch64" ;;
                *)
                    echo "fetch-slang: unsupported Linux architecture '$UNAME_M'." >&2
                    echo "fetch-slang: pass --triple <value> to override (e.g. linux-x86_64)." >&2
                    exit 1
                    ;;
            esac
            ;;
        Darwin)
            case "$UNAME_M" in
                arm64|aarch64) TRIPLE="macos-aarch64" ;;
                x86_64)        TRIPLE="macos-x86_64" ;;
                *)
                    echo "fetch-slang: unsupported macOS architecture '$UNAME_M'." >&2
                    echo "fetch-slang: pass --triple <value> to override (e.g. macos-aarch64)." >&2
                    exit 1
                    ;;
            esac
            ;;
        *)
            echo "fetch-slang: unsupported host OS '$UNAME_S'." >&2
            echo "fetch-slang: this script supports Linux + macOS; on Windows use tools/fetch-slang.ps1." >&2
            exit 1
            ;;
    esac
fi

echo "fetch-slang: target host triple   = $TRIPLE"

# --- Resolve cache dir ------------------------------------------------------
CACHE_ROOT="${SHADER_CLIPPY_SLANG_CACHE:-}"
if [[ -z "$CACHE_ROOT" ]]; then
    if [[ -z "${HOME:-}" ]]; then
        echo "fetch-slang: neither SHADER_CLIPPY_SLANG_CACHE nor HOME is set." >&2
        exit 1
    fi
    CACHE_ROOT="$HOME/.cache/shader-clippy/slang"
fi
CACHE_DIR="$CACHE_ROOT/$VERSION"

# --- Force-clean if requested ----------------------------------------------
if [[ "$FORCE" -eq 1 && -d "$CACHE_DIR" ]]; then
    echo "fetch-slang: --force given; removing existing $CACHE_DIR"
    rm -rf "$CACHE_DIR"
fi

# --- Idempotency check ------------------------------------------------------
INCLUDE_DIR="$CACHE_DIR/include"
if [[ -d "$INCLUDE_DIR" ]] && [[ -n "$(ls -A "$INCLUDE_DIR" 2>/dev/null || true)" ]]; then
    echo "Slang $VERSION already cached at $CACHE_DIR"
    exit 0
fi

mkdir -p "$CACHE_DIR"

# --- Download --------------------------------------------------------------
URL="https://github.com/shader-slang/slang/releases/download/v${VERSION}/slang-${VERSION}-${TRIPLE}.tar.gz"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
TMP_TGZ="$TMP_DIR/slang-${VERSION}-${TRIPLE}.tar.gz"

echo "fetch-slang: downloading $URL"
echo "fetch-slang: -> $TMP_TGZ"

if command -v curl >/dev/null 2>&1; then
    if ! curl -fL --retry 3 -o "$TMP_TGZ" "$URL"; then
        echo "fetch-slang: curl download failed for $URL" >&2
        exit 1
    fi
elif command -v wget >/dev/null 2>&1; then
    if ! wget -O "$TMP_TGZ" "$URL"; then
        echo "fetch-slang: wget download failed for $URL" >&2
        exit 1
    fi
else
    echo "fetch-slang: neither curl nor wget is available." >&2
    exit 1
fi

# --- Verify it's a gzip (1F 8B magic) --------------------------------------
MAGIC="$(head -c 2 "$TMP_TGZ" | od -An -tx1 | tr -d ' \n')"
if [[ "$MAGIC" != "1f8b" ]]; then
    echo "fetch-slang: downloaded file is not a valid gzip archive (bad magic '$MAGIC')." >&2
    echo "fetch-slang: URL may be wrong or release missing: $URL" >&2
    exit 1
fi

# --- SHA-256 verification (optional but strongly recommended) ---------------
# When `SHADER_CLIPPY_SLANG_SHA256` is set, the downloaded tarball's hash MUST
# match exactly. Mismatch → abort, leaving the cache untouched. Unset →
# warn-and-continue (the gzip-magic check above is the only integrity gate).
#
# Per-triple expected hashes can also be supplied via
# `SHADER_CLIPPY_SLANG_SHA256_<UPPER_TRIPLE_WITH_UNDERSCORES>`; e.g. for
# the `linux-x86_64` tarball, set `SHADER_CLIPPY_SLANG_SHA256_LINUX_X86_64`.
# The per-triple variable wins over the generic one when both are set.
EXPECTED_SHA256=""
TRIPLE_VAR="SHADER_CLIPPY_SLANG_SHA256_$(echo "$TRIPLE" | tr 'a-z-' 'A-Z_')"
if [[ -n "${!TRIPLE_VAR:-}" ]]; then
    EXPECTED_SHA256="${!TRIPLE_VAR}"
elif [[ -n "${SHADER_CLIPPY_SLANG_SHA256:-}" ]]; then
    EXPECTED_SHA256="${SHADER_CLIPPY_SLANG_SHA256}"
fi

if [[ -n "$EXPECTED_SHA256" ]]; then
    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL_SHA256="$(sha256sum "$TMP_TGZ" | awk '{print $1}')"
    elif command -v shasum >/dev/null 2>&1; then
        ACTUAL_SHA256="$(shasum -a 256 "$TMP_TGZ" | awk '{print $1}')"
    else
        echo "fetch-slang: neither sha256sum nor shasum available; cannot verify SHA-256." >&2
        exit 1
    fi
    if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
        echo "fetch-slang: SHA-256 mismatch for $TMP_TGZ" >&2
        echo "fetch-slang:   expected: $EXPECTED_SHA256" >&2
        echo "fetch-slang:   actual:   $ACTUAL_SHA256" >&2
        echo "fetch-slang: refusing to populate cache. Possible MITM or" >&2
        echo "fetch-slang: tampered upstream — verify the release manually." >&2
        exit 1
    fi
    echo "fetch-slang: SHA-256 verified ($ACTUAL_SHA256)"
else
    echo "fetch-slang: warning — no SHADER_CLIPPY_SLANG_SHA256 set; skipping integrity verification." >&2
    echo "fetch-slang: set SHADER_CLIPPY_SLANG_SHA256_${TRIPLE_VAR#SHADER_CLIPPY_SLANG_SHA256_} for hardened CI." >&2
fi

# --- Extract ---------------------------------------------------------------
echo "fetch-slang: extracting to $CACHE_DIR"
if ! tar -xzf "$TMP_TGZ" -C "$CACHE_DIR"; then
    echo "fetch-slang: tar extraction failed." >&2
    exit 1
fi

# --- Sanity-check the layout ------------------------------------------------
case "$TRIPLE" in
    macos-*) EXPECTED_LIB="lib/libslang.dylib" ;;
    *)       EXPECTED_LIB="lib/libslang.so" ;;
esac

if [[ ! -f "$CACHE_DIR/include/slang.h" ]]; then
    echo "fetch-slang: extracted archive does not contain include/slang.h directly under $CACHE_DIR." >&2
    echo "fetch-slang: contents:" >&2
    ls -la "$CACHE_DIR" >&2 || true
    echo "fetch-slang: UseSlang.cmake expects include/slang.h + ${EXPECTED_LIB}. Inspect the layout." >&2
    exit 1
fi

echo
echo "Slang $VERSION cached at: $CACHE_DIR"
echo "CMake will pick this up automatically on the next configure."
