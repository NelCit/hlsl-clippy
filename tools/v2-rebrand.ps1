#!/usr/bin/env pwsh
# v2.0 rebrand: hlsl-clippy -> shader-clippy
#
# One-shot bulk text replacement across the repo. Run from repo root.
# Exclusions: .git, external/{slang,tree-sitter*,tomlplusplus,...} (vendored),
# build*, **/node_modules, **/out, vscode-extension/.vsix.

$ErrorActionPreference = 'Stop'

# PowerShell hash-literal keys are case-insensitive, so use a list of pairs
# to keep distinct case-variants.
$replacements = @(
    @('hlsl_clippy', 'shader_clippy'),
    @('hlsl-clippy', 'shader-clippy'),
    @('hlslClippy',  'shaderClippy'),
    @('HLSL_CLIPPY', 'SHADER_CLIPPY'),
    @('HLSL Clippy', 'Shader Clippy'),
    @('hlsl clippy', 'shader clippy')
)

$repoRoot = (Get-Location).Path
Write-Host "v2-rebrand: scanning $repoRoot"

# Use git ls-files so we honour .gitignore + only operate on tracked files.
# Then layer in additional excludes for vendored content under external/.
$tracked = git ls-files
$excludeRegex = '^(external/(slang|tree-sitter|tree-sitter-hlsl|tree-sitter-slang|tomlplusplus)/|build|vscode-extension/(node_modules|out)/|.*\.vsix$)'

$candidates = $tracked | Where-Object { $_ -notmatch $excludeRegex }
Write-Host "v2-rebrand: $($candidates.Count) candidate files (after exclude)"

$touched = 0
$skippedBinary = 0

foreach ($rel in $candidates) {
    $abs = Join-Path $repoRoot $rel
    if (-not (Test-Path -LiteralPath $abs -PathType Leaf)) { continue }

    # Skip binaries / images / archives.
    $ext = [IO.Path]::GetExtension($abs).ToLower()
    if ($ext -in '.png','.jpg','.jpeg','.gif','.ico','.icns','.zip','.tar','.gz','.tgz','.7z','.exe','.dll','.lib','.obj','.o','.a','.so','.dylib','.pdb','.bin','.vsix','.dat','.wasm','.ttf','.woff','.woff2') {
        continue
    }

    try {
        $content = [IO.File]::ReadAllText($abs)
    } catch {
        $skippedBinary++
        continue
    }
    $original = $content

    foreach ($pair in $replacements) {
        $from = $pair[0]
        $to   = $pair[1]
        if ($content.Contains($from)) {
            $content = $content.Replace($from, $to)
        }
    }

    if ($content -ne $original) {
        # Preserve original line endings: detect if file has CRLF.
        $hadCRLF = $original.Contains("`r`n")
        if (-not $hadCRLF) {
            $content = $content -replace "`r`n", "`n"
        }
        [IO.File]::WriteAllText($abs, $content)
        $touched++
    }
}

Write-Host "v2-rebrand: rewrote $touched files (skipped $skippedBinary binaries)"
