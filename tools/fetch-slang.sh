#!/usr/bin/env bash
#
# tools/fetch-slang.sh
#
# Linux equivalent of tools/fetch-slang.ps1. Populates the per-user
# prebuilt Slang cache so fresh clones / new git worktrees do not have to
# rebuild Slang from source (~20 minutes cold).
#
# What it does
#   1. Reads the pinned Slang version from cmake/SlangVersion.cmake
#      (regex-parsed; CMake is NOT invoked).
#   2. Computes the cache target dir:
#        $HOME/.cache/hlsl-clippy/slang/<version>/
#      (or $HLSL_CLIPPY_SLANG_CACHE/<version>/ if that env var is set)
#   3. If that dir already has a non-empty include/ subdir, exits 0.
#   4. Otherwise downloads
#        https://github.com/shader-slang/slang/releases/download/v<version>/slang-<version>-linux-x86_64.tar.gz
#      to a temp file, untars it into the cache dir, and cleans up.
#
# Usage (from a fresh clone, repo root):
#   bash tools/fetch-slang.sh                    # idempotent
#   bash tools/fetch-slang.sh --force            # wipe + redownload
#   bash tools/fetch-slang.sh --version 2026.7.1 # override pinned version
#
# Requires: bash, curl OR wget, tar, sed/grep. No CMake invocation.

set -euo pipefail

# --- Arg parsing -----------------------------------------------------------
FORCE=0
VERSION=""
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
        -h|--help)
            sed -n '3,28p' "$0"
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
        echo "fetch-slang: cannot find $VERSION_FILE. Are you running this from a clone of hlsl-clippy?" >&2
        exit 1
    fi
    # Match: set(HLSL_CLIPPY_SLANG_VERSION "X.Y.Z" ...)
    VERSION="$(grep -oE 'HLSL_CLIPPY_SLANG_VERSION[[:space:]]+"[^"]+"' "$VERSION_FILE" \
        | head -n1 \
        | sed -E 's/.*"([^"]+)".*/\1/')"
    if [[ -z "$VERSION" ]]; then
        echo "fetch-slang: failed to parse HLSL_CLIPPY_SLANG_VERSION from $VERSION_FILE" >&2
        exit 1
    fi
fi

echo "fetch-slang: target Slang version = $VERSION"

# --- Resolve cache dir ------------------------------------------------------
CACHE_ROOT="${HLSL_CLIPPY_SLANG_CACHE:-}"
if [[ -z "$CACHE_ROOT" ]]; then
    if [[ -z "${HOME:-}" ]]; then
        echo "fetch-slang: neither HLSL_CLIPPY_SLANG_CACHE nor HOME is set." >&2
        exit 1
    fi
    CACHE_ROOT="$HOME/.cache/hlsl-clippy/slang"
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
URL="https://github.com/shader-slang/slang/releases/download/v${VERSION}/slang-${VERSION}-linux-x86_64.tar.gz"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
TMP_TGZ="$TMP_DIR/slang-${VERSION}-linux-x86_64.tar.gz"

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

# --- Extract ---------------------------------------------------------------
echo "fetch-slang: extracting to $CACHE_DIR"
if ! tar -xzf "$TMP_TGZ" -C "$CACHE_DIR"; then
    echo "fetch-slang: tar extraction failed." >&2
    exit 1
fi

# --- Sanity-check the layout ------------------------------------------------
if [[ ! -f "$CACHE_DIR/include/slang.h" ]]; then
    echo "fetch-slang: extracted archive does not contain include/slang.h directly under $CACHE_DIR." >&2
    echo "fetch-slang: contents:" >&2
    ls -la "$CACHE_DIR" >&2 || true
    echo "fetch-slang: UseSlang.cmake expects include/slang.h + lib/libslang.so. Inspect the layout." >&2
    exit 1
fi

echo
echo "Slang $VERSION cached at: $CACHE_DIR"
echo "CMake will pick this up automatically on the next configure."
