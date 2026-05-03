# Install shader-clippy git hooks into .git/hooks/ (Windows / PowerShell).
#
# Idempotent: re-running overwrites existing hooks.
#
# Usage:
#   pwsh tools\install-hooks.ps1

$ErrorActionPreference = 'Stop'

# Resolve the repo root from this script's location so the installer works
# regardless of the caller's cwd.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$SrcDir    = Join-Path $RepoRoot 'tools\git-hooks'

# Respect core.hooksPath if the user has overridden it; fall back to
# .git/hooks otherwise. `git rev-parse --git-path hooks` does both at once.
$HooksDir = $null
try {
    $HooksDir = (& git -C $RepoRoot rev-parse --git-path hooks 2>$null).Trim()
} catch {
    # git not on PATH or not a repo — fall through to the manual path.
}
if ([string]::IsNullOrEmpty($HooksDir)) {
    $HooksDir = Join-Path $RepoRoot '.git\hooks'
} elseif (-not [System.IO.Path]::IsPathRooted($HooksDir)) {
    $HooksDir = Join-Path $RepoRoot $HooksDir
}

if (-not (Test-Path $HooksDir)) {
    New-Item -ItemType Directory -Force -Path $HooksDir | Out-Null
}

$Hooks = @('pre-commit')

foreach ($hook in $Hooks) {
    $src = Join-Path $SrcDir   $hook
    $dst = Join-Path $HooksDir $hook
    if (-not (Test-Path $src)) {
        Write-Error "install-hooks: missing source hook: $src"
        exit 1
    }
    # Overwrite any pre-existing hook (idempotent).
    Copy-Item -Path $src -Destination $dst -Force
    # On Git for Windows the hook is invoked through sh.exe, so the file
    # mode is irrelevant — we just need the script body in place. The
    # extension-less filename is what Git matches.
    Write-Host "installed: tools/git-hooks/$hook -> $dst"
}

Write-Host ""
Write-Host "Pre-commit hook installed. Override env vars:"
Write-Host "  CLANG_FORMAT=C:\path\to\clang-format-18.exe    (explicit binary)"
Write-Host "  SHADER_CLIPPY_HOOK_ALLOW_ANY_CLANG_FORMAT=1      (skip v18 check)"
Write-Host "  SHADER_CLIPPY_HOOK_FIX=1                         (auto-fix + re-stage)"
