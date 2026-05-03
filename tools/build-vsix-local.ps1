# Build the VS Code extension locally and install it into your real
# VS Code so you can test BEFORE tagging a release. This script mirrors
# the steps `release-vscode.yml` runs in CI, minus the Marketplace
# publish.
#
# Steps:
#   1. Build shader_clippy_lsp via cmake (uses the existing dev-shell.ps1
#      to set up clang-cl + Slang prebuilt on PATH).
#   2. Stage shader-clippy-lsp.exe + every sibling Slang DLL into
#      vscode-extension/server/windows-x86_64/.
#   3. Compile the TypeScript source (catches strict-mode errors that
#      would otherwise blow up release-vscode.yml's "Compile TypeScript"
#      step at tag time).
#   4. Run `vsce package --target win32-x64` to produce a .vsix.
#   5. Install the .vsix into your VS Code via `code --install-extension`.
#
# Run from repo root in a regular PowerShell prompt:
#   powershell -ExecutionPolicy Bypass -File tools\build-vsix-local.ps1
#
# Optional flags via environment variables:
#   SHADER_CLIPPY_SKIP_INSTALL=1   build the .vsix but don't install it
#   SHADER_CLIPPY_SKIP_BUILD=1     reuse the existing build/ tree

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# 0. Arm the dev shell (clang-cl + cmake/ninja + Slang DLLs on PATH).
. "$PSScriptRoot\dev-shell.ps1" *> dev-shell.log
if (-not $env:SHADER_CLIPPY_DEV_SHELL_READY) {
    Write-Error "dev-shell.ps1 did not arm; check dev-shell.log"
    exit 1
}

# 1. Build shader_clippy_lsp (skip if requested).
if (-not $env:SHADER_CLIPPY_SKIP_BUILD) {
    Write-Host '==> Building shader_clippy_lsp...' -ForegroundColor Cyan
    cmake --build build --target shader_clippy_lsp 2>&1 | Tee-Object -FilePath dev-shell.log -Append | Select-Object -Last 5
    if ($LASTEXITCODE -ne 0) {
        Write-Error "cmake build failed; see dev-shell.log"
        exit 1
    }
}

# 2. Stage LSP exe + sibling DLLs into vscode-extension/server/<platform>/.
$platform = 'windows-x86_64'
$dest = Join-Path $root "vscode-extension\server\$platform"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$lspSrc = $null
foreach ($c in @(".\build\lsp\shader-clippy-lsp.exe", ".\build\lsp\Release\shader-clippy-lsp.exe", ".\build\shader-clippy-lsp.exe")) {
    if (Test-Path $c) { $lspSrc = $c; break }
}
if (-not $lspSrc) {
    Write-Error "could not locate built shader-clippy-lsp.exe"
    exit 1
}
Write-Host "==> Staging $lspSrc + sibling DLLs into $dest" -ForegroundColor Cyan
Copy-Item $lspSrc "$dest/shader-clippy-lsp.exe" -Force
$srcDir = Split-Path -Parent $lspSrc
foreach ($dll in Get-ChildItem -Path $srcDir -Filter '*.dll' -File) {
    Copy-Item $dll.FullName "$dest/" -Force
    Write-Host "    bundled $($dll.Name) ($([math]::Round($dll.Length / 1MB, 1)) MB)"
}

# 3. Compile TypeScript.
Push-Location vscode-extension
try {
    if (-not (Test-Path node_modules)) {
        Write-Host '==> npm install...' -ForegroundColor Cyan
        npm install --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) { exit 1 }
    }
    Write-Host '==> Compiling TypeScript...' -ForegroundColor Cyan
    npx tsc -p .
    if ($LASTEXITCODE -ne 0) {
        Write-Error 'TypeScript compile failed.'
        exit 1
    }

    # 4. Package per-platform .vsix.
    $version = (Get-Content package.json | ConvertFrom-Json).version
    $vsixName = "shader-clippy-$version-win32-x64-local.vsix"
    Write-Host "==> Packaging $vsixName..." -ForegroundColor Cyan
    npx --yes @vscode/vsce package --target win32-x64 --out $vsixName
    if ($LASTEXITCODE -ne 0) {
        Write-Error 'vsce package failed.'
        exit 1
    }

    $vsixSize = [math]::Round((Get-Item $vsixName).Length / 1MB, 1)
    Write-Host ""
    Write-Host "Built $vsixName ($vsixSize MB)" -ForegroundColor Green

    # 5. Install into local VS Code.
    if ($env:SHADER_CLIPPY_SKIP_INSTALL) {
        Write-Host "Skipping install (SHADER_CLIPPY_SKIP_INSTALL=1)." -ForegroundColor Yellow
        Write-Host "To install manually:"
        Write-Host "  code --install-extension vscode-extension\$vsixName --force"
    } elseif (Get-Command code -ErrorAction SilentlyContinue) {
        Write-Host '==> Installing into VS Code...' -ForegroundColor Cyan
        code --install-extension $vsixName --force
        Write-Host ""
        Write-Host "Installed. Reload VS Code (Ctrl+Shift+P -> Reload Window) and open a .hlsl file." -ForegroundColor Green
    } else {
        Write-Host "'code' CLI not on PATH; install manually:" -ForegroundColor Yellow
        Write-Host "  code --install-extension vscode-extension\$vsixName --force"
    }
} finally {
    Pop-Location
}
