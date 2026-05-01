#!/usr/bin/env bash
# Install hlsl-clippy git hooks into .git/hooks/.
#
# Idempotent: re-running overwrites existing hooks. POSIX-friendly aside
# from the bash shebang for argument parsing — the hook itself is /bin/sh.
#
# Usage:
#   bash tools/install-hooks.sh

set -euo pipefail

# Resolve the repo root from this script's location so the installer works
# regardless of the caller's cwd.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$REPO_ROOT/tools/git-hooks"

# Locate the hooks directory. Respect core.hooksPath if the user has set it
# (some teams centralise hooks); fall back to .git/hooks otherwise.
HOOKS_DIR="$(git -C "$REPO_ROOT" rev-parse --git-path hooks 2>/dev/null || true)"
if [ -z "$HOOKS_DIR" ]; then
    HOOKS_DIR="$REPO_ROOT/.git/hooks"
fi

mkdir -p "$HOOKS_DIR"

HOOKS=(pre-commit)

for hook in "${HOOKS[@]}"; do
    src="$SRC_DIR/$hook"
    dst="$HOOKS_DIR/$hook"
    if [ ! -f "$src" ]; then
        echo "install-hooks: missing source hook: $src" >&2
        exit 1
    fi
    # Overwrite any pre-existing hook (idempotent).
    rm -f "$dst"
    cp "$src" "$dst"
    chmod +x "$dst"
    echo "installed: tools/git-hooks/$hook -> $dst"
done

echo ""
echo "Pre-commit hook installed. Override env vars:"
echo "  CLANG_FORMAT=/path/to/clang-format-18           (explicit binary)"
echo "  HLSL_CLIPPY_HOOK_ALLOW_ANY_CLANG_FORMAT=1       (skip v18 check)"
echo "  HLSL_CLIPPY_HOOK_FIX=1                          (auto-fix + re-stage)"
