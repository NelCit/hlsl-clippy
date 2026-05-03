#!/usr/bin/env bash
# Build the VS Code extension locally and install it into your real
# VS Code so you can test BEFORE tagging a release. POSIX equivalent of
# tools/build-vsix-local.ps1; same five steps:
#
#   1. cmake build shader_clippy_lsp.
#   2. Stage shader-clippy-lsp + sibling shared libs into
#      vscode-extension/server/<platform>/.
#   3. npx tsc -p ./vscode-extension (catches strict-mode TS errors).
#   4. vsce package --target <platform>.
#   5. code --install-extension <built>.vsix --force.
#
# Run from repo root:
#   bash tools/build-vsix-local.sh
#
# Optional environment variables:
#   SHADER_CLIPPY_SKIP_INSTALL=1   build the .vsix but don't install it
#   SHADER_CLIPPY_SKIP_BUILD=1     reuse the existing build/ tree

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# Detect platform tuple matching `currentPlatform()` in
# vscode-extension/src/download.ts (linux-x86_64 / macos-aarch64 /
# macos-x86_64). The vsce target uses a different vocabulary
# (linux-x64 / darwin-arm64 / darwin-x64).
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)   platform=linux-x86_64;   vsce_target=linux-x64    ;;
    Darwin-arm64)   platform=macos-aarch64;  vsce_target=darwin-arm64 ;;
    Darwin-x86_64)  platform=macos-x86_64;   vsce_target=darwin-x64   ;;
    *)
        echo "Unsupported platform: $(uname -s)-$(uname -m)" >&2
        exit 1
        ;;
esac

# 1. Build shader_clippy_lsp.
if [ -z "${SHADER_CLIPPY_SKIP_BUILD:-}" ]; then
    echo "==> Building shader_clippy_lsp..."
    cmake --build build --target shader_clippy_lsp >/dev/null
fi

# 2. Stage LSP exe + sibling shared libs.
dest="vscode-extension/server/$platform"
mkdir -p "$dest"
src=""
for c in ./build/lsp/shader-clippy-lsp ./build/shader-clippy-lsp; do
    if [ -x "$c" ]; then
        src="$c"
        break
    fi
done
if [ -z "$src" ]; then
    echo "ERROR: could not locate shader-clippy-lsp binary" >&2
    exit 1
fi
echo "==> Staging $src + sibling libs into $dest"
cp "$src" "$dest/"
src_dir="$(dirname "$src")"
for lib in "$src_dir"/*.so "$src_dir"/*.so.* "$src_dir"/*.dylib; do
    if [ -f "$lib" ]; then
        cp "$lib" "$dest/"
        echo "    bundled $(basename "$lib")"
    fi
done 2>/dev/null

# 3. Compile TypeScript.
cd vscode-extension
if [ ! -d node_modules ]; then
    echo "==> npm install..."
    npm install --no-audit --no-fund
fi
echo "==> Compiling TypeScript..."
npx tsc -p .

# 4. Package per-platform .vsix.
version=$(node -p "require('./package.json').version")
vsix="shader-clippy-${version}-${vsce_target}-local.vsix"
echo "==> Packaging $vsix..."
npx --yes @vscode/vsce package --target "$vsce_target" --out "$vsix"

vsix_size=$(du -h "$vsix" | cut -f1)
echo
echo "Built $vsix ($vsix_size)"

# 5. Install into local VS Code.
if [ -n "${SHADER_CLIPPY_SKIP_INSTALL:-}" ]; then
    echo "Skipping install (SHADER_CLIPPY_SKIP_INSTALL=1)."
    echo "To install manually: code --install-extension vscode-extension/$vsix --force"
elif command -v code >/dev/null; then
    echo "==> Installing into VS Code..."
    code --install-extension "$vsix" --force
    echo
    echo "Installed. Reload VS Code (Ctrl+Shift+P -> Reload Window) and open a .hlsl file."
else
    echo "'code' CLI not on PATH; install manually:"
    echo "  code --install-extension vscode-extension/$vsix --force"
fi
