# Second-pass sweep over docs/rules/*.md to catch the alternate
# "Pre-v0 status:" placeholder phrasing that the first sweeper missed.
# For shipped rules (those with a matching .cpp under core/src/rules/),
# replace the banner with the same "shipped (Phase N)" form. Doc-only
# stubs (no .cpp) keep the original wording.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$docs = Join-Path $root 'docs\rules'
$rules = Join-Path $root 'core\src\rules'

# em-dash kept as a char-code so the script file stays ASCII.
$emDash = [char]0x2014

# Match the alternate banner verbatim (one line, ends with the period
# before the markdown blank line).
$staleRe = '(?m)^> \*\*Pre-v0 status:\*\* this rule page is published ahead of the implementation\. Behaviour described here reflects the design intent; the rule is not yet enforced by the tool\.'

$rewritten = 0
$skippedStub = 0

foreach ($md in Get-ChildItem -Path $docs -Filter '*.md' -File) {
    if ($md.Name -eq 'index.md' -or $md.Name -eq '_template.md') { continue }
    $text = [System.IO.File]::ReadAllText($md.FullName)
    if ($text -notmatch $staleRe) { continue }

    $base = $md.BaseName -replace '-', '_'
    $cpp = Join-Path $rules ($base + '.cpp')
    if (-not (Test-Path $cpp)) {
        $skippedStub++
        continue
    }

    # Pull `phase: N` from the frontmatter for the new banner.
    if ($text -notmatch '(?m)^phase:\s*(\d+)') { continue }
    $phase = [int]$matches[1]

    $newBanner = '> **Status:** shipped (Phase ' + $phase + ') ' + $emDash + ' see [CHANGELOG](../../CHANGELOG.md).'
    $newText = [regex]::Replace($text, $staleRe, $newBanner)
    [System.IO.File]::WriteAllText($md.FullName, $newText)
    $rewritten++
}

Write-Output "rewritten: $rewritten"
Write-Output "skipped (still doc-only stubs): $skippedStub"
