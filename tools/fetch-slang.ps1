# tools/fetch-slang.ps1
#
# Populate the per-user prebuilt Slang cache so that fresh clones and new
# git worktrees do not have to rebuild Slang from source (~20 minutes cold).
#
# What it does
#   1. Reads the pinned Slang version from cmake/SlangVersion.cmake
#      (regex-parsed; CMake is NOT invoked).
#   2. Computes the cache target dir:
#        %LOCALAPPDATA%\hlsl-clippy\slang\<version>\
#   3. If that dir already has a non-empty include\ subdir, exits 0 (cached).
#   4. Otherwise downloads
#        https://github.com/shader-slang/slang/releases/download/v<version>/slang-<version>-windows-x86_64.zip
#      to a temp file, extracts it into the cache dir, and cleans up.
#
# Usage (from a fresh clone, repo root):
#   pwsh tools\fetch-slang.ps1            # idempotent; no-ops if cached
#   pwsh tools\fetch-slang.ps1 -Force     # wipe + redownload
#   pwsh tools\fetch-slang.ps1 -Version 2026.7.1  # override pinned version
#
# Targets PowerShell 5.1 (the default Windows shell) — no PS7-only syntax
# (no `&&` / `||`, no ternary, no null-coalescing, no `?.`).
#
# Output: the absolute cache path is printed on success. On failure the
# script writes an error and exits non-zero.

[CmdletBinding()]
param(
    [switch]$Force,
    [string]$Version
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Locate the repo root (parent of this script's tools/ dir) -------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
$VersionFile = Join-Path $RepoRoot 'cmake\SlangVersion.cmake'

# --- Resolve the pinned version --------------------------------------------
if (-not $Version -or $Version -eq '') {
    if (-not (Test-Path -LiteralPath $VersionFile)) {
        Write-Error "fetch-slang: cannot find $VersionFile. Are you running this from a clone of hlsl-clippy?"
        exit 1
    }

    $content = Get-Content -LiteralPath $VersionFile -Raw
    # Match: set(HLSL_CLIPPY_SLANG_VERSION "X.Y.Z" ...)
    $match = [regex]::Match(
        $content,
        'set\s*\(\s*HLSL_CLIPPY_SLANG_VERSION\s+"([^"]+)"'
    )
    if (-not $match.Success) {
        Write-Error "fetch-slang: failed to parse HLSL_CLIPPY_SLANG_VERSION from $VersionFile."
        exit 1
    }
    $Version = $match.Groups[1].Value
}

Write-Host "fetch-slang: target Slang version = $Version"

# --- Resolve the cache dir --------------------------------------------------
$CacheRoot = $env:HLSL_CLIPPY_SLANG_CACHE
if (-not $CacheRoot -or $CacheRoot -eq '') {
    if (-not $env:LOCALAPPDATA -or $env:LOCALAPPDATA -eq '') {
        Write-Error "fetch-slang: neither HLSL_CLIPPY_SLANG_CACHE nor LOCALAPPDATA is set."
        exit 1
    }
    $CacheRoot = Join-Path $env:LOCALAPPDATA 'hlsl-clippy\slang'
}
$CacheDir = Join-Path $CacheRoot $Version

# --- Force-clean if requested ----------------------------------------------
if ($Force -and (Test-Path -LiteralPath $CacheDir)) {
    Write-Host "fetch-slang: -Force given; removing existing $CacheDir"
    Remove-Item -LiteralPath $CacheDir -Recurse -Force
}

# --- Idempotency check ------------------------------------------------------
$IncludeDir = Join-Path $CacheDir 'include'
if ((Test-Path -LiteralPath $IncludeDir) -and
    (Get-ChildItem -LiteralPath $IncludeDir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    Write-Host "Slang $Version already cached at $CacheDir"
    exit 0
}

# --- Prepare cache dir ------------------------------------------------------
if (-not (Test-Path -LiteralPath $CacheDir)) {
    New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
}

# --- Download --------------------------------------------------------------
$Url = "https://github.com/shader-slang/slang/releases/download/v$Version/slang-$Version-windows-x86_64.zip"
$TempZip = Join-Path ([System.IO.Path]::GetTempPath()) ("slang-" + $Version + "-" + [System.Guid]::NewGuid().ToString('N') + '.zip')

Write-Host "fetch-slang: downloading $Url"
Write-Host "fetch-slang: -> $TempZip"

# Force TLS 1.2 (PowerShell 5.1 defaults to SSL3/TLS1.0 on some images).
try {
    [System.Net.ServicePointManager]::SecurityProtocol =
        [System.Net.ServicePointManager]::SecurityProtocol -bor [System.Net.SecurityProtocolType]::Tls12
} catch {
    # Older PS may lack Tls12 enum value; ignore and let the download try.
}

# Use Invoke-WebRequest with -UseBasicParsing (5.1-friendly; avoids IE engine).
$ProgressPreference = 'Continue'
try {
    Invoke-WebRequest -Uri $Url -OutFile $TempZip -UseBasicParsing
} catch {
    Write-Error "fetch-slang: download failed: $($_.Exception.Message)"
    if (Test-Path -LiteralPath $TempZip) { Remove-Item -LiteralPath $TempZip -Force }
    exit 1
}

# --- Verify it's a zip (PK\x03\x04 magic) -----------------------------------
try {
    $stream = [System.IO.File]::OpenRead($TempZip)
    $magic = New-Object byte[] 4
    [void]$stream.Read($magic, 0, 4)
    $stream.Close()
} catch {
    Write-Error "fetch-slang: cannot open downloaded file $TempZip"
    if (Test-Path -LiteralPath $TempZip) { Remove-Item -LiteralPath $TempZip -Force }
    exit 1
}

if (-not ($magic[0] -eq 0x50 -and $magic[1] -eq 0x4B -and $magic[2] -eq 0x03 -and $magic[3] -eq 0x04)) {
    Write-Error "fetch-slang: downloaded file is not a valid zip (bad magic). URL may be wrong or release missing: $Url"
    Remove-Item -LiteralPath $TempZip -Force
    exit 1
}

# --- SHA-256 verification (optional but strongly recommended) --------------
# When `HLSL_CLIPPY_SLANG_SHA256` (or the per-triple variant
# `HLSL_CLIPPY_SLANG_SHA256_WINDOWS_X86_64`) is set, the downloaded zip's
# hash MUST match exactly. Mismatch → abort, leaving the cache untouched.
# Unset → warn-and-continue (the zip-magic check above is the only gate).
$TripleVarName = 'HLSL_CLIPPY_SLANG_SHA256_' + ($Triple.ToUpper() -replace '-', '_')
$ExpectedSha256 = [System.Environment]::GetEnvironmentVariable($TripleVarName)
if (-not $ExpectedSha256) {
    $ExpectedSha256 = $env:HLSL_CLIPPY_SLANG_SHA256
}

if ($ExpectedSha256) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $TempZip).Hash.ToLower()
    $expected = $ExpectedSha256.ToLower()
    if ($actual -ne $expected) {
        Write-Error "fetch-slang: SHA-256 mismatch for $TempZip"
        Write-Error "fetch-slang:   expected: $expected"
        Write-Error "fetch-slang:   actual:   $actual"
        Write-Error "fetch-slang: refusing to populate cache. Possible MITM or tampered upstream."
        Remove-Item -LiteralPath $TempZip -Force
        exit 1
    }
    Write-Host "fetch-slang: SHA-256 verified ($actual)"
} else {
    Write-Warning "fetch-slang: no $TripleVarName / HLSL_CLIPPY_SLANG_SHA256 set; skipping integrity verification."
    Write-Warning "fetch-slang: set the env var for hardened CI runs."
}

# --- Extract ---------------------------------------------------------------
Write-Host "fetch-slang: extracting to $CacheDir"
try {
    Expand-Archive -LiteralPath $TempZip -DestinationPath $CacheDir -Force
} catch {
    Write-Error "fetch-slang: extraction failed: $($_.Exception.Message)"
    Remove-Item -LiteralPath $TempZip -Force
    exit 1
}

# --- Clean up --------------------------------------------------------------
Remove-Item -LiteralPath $TempZip -Force

# --- Sanity-check the layout ------------------------------------------------
if (-not (Test-Path -LiteralPath (Join-Path $CacheDir 'include\slang.h'))) {
    Write-Warning "fetch-slang: extracted archive does not contain include\slang.h directly under $CacheDir."
    Write-Warning "fetch-slang: contents:"
    Get-ChildItem -LiteralPath $CacheDir -Force | ForEach-Object { Write-Warning "  $($_.Name)" }
    Write-Warning "fetch-slang: UseSlang.cmake expects include/slang.h + lib/slang.lib + bin/slang.dll. Inspect the layout."
    exit 1
}

Write-Host ""
Write-Host "Slang $Version cached at: $CacheDir"
Write-Host "CMake will pick this up automatically on the next configure."
exit 0
