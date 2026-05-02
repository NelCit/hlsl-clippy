# Sweep docs/rules/*.md and replace stale "pre-v0 -- rule scheduled for Phase N"
# Status banners with "shipped (Phase N)" for any rule whose .cpp lives in
# core/src/rules/. Doc-only stubs (no matching .cpp) are left alone.
#
# One-shot script, kept under tools/ for the audit trail.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$docs = Join-Path $root 'docs\rules'
$rules = Join-Path $root 'core\src\rules'

# em-dash codepoint (U+2014) constructed from char code so the script file
# itself stays ASCII; the docs use em-dash and we want byte-for-byte parity.
$emDash = [char]0x2014
$staleRe = '(?m)^> \*\*Status:\*\* pre-v0 ' + $emDash + ' rule scheduled for Phase (\d+); see \[ROADMAP\]\(\.\./\.\./ROADMAP\.md\)\.'

$rewritten = 0
$skippedStub = 0

foreach ($md in Get-ChildItem -Path $docs -Filter '*.md' -File) {
    if ($md.Name -eq 'index.md' -or $md.Name -eq '_template.md') { continue }
    $text = [System.IO.File]::ReadAllText($md.FullName)
    if ($text -notmatch $staleRe) { continue }
    $phase = [int]$matches[1]
    $base = $md.BaseName -replace '-', '_'
    $cpp = Join-Path $rules ($base + '.cpp')
    if (-not (Test-Path $cpp)) {
        $skippedStub++
        continue
    }
    $newBanner = '> **Status:** shipped (Phase ' + $phase + ') ' + $emDash + ' see [CHANGELOG](../../CHANGELOG.md).'
    $newText = [regex]::Replace($text, $staleRe, $newBanner)
    [System.IO.File]::WriteAllText($md.FullName, $newText)
    $rewritten++
}

Write-Output "rewritten: $rewritten"
Write-Output "skipped (still doc-only stubs): $skippedStub"
