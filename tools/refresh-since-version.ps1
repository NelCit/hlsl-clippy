# Sweep docs/rules/*.md and rewrite `since-version:` in the YAML
# frontmatter to v0.5.0 for every rule whose .cpp exists in core/src/rules/.
# Doc-only stubs (no .cpp) keep their original aspirational version.
#
# Why: the original since-version values (v0.2.0, v0.3.0, v0.4.0, v0.7.0)
# were placeholders matching the original phase plan. The actual launch
# release was v0.5.0; the v0.5.1-v0.5.6 chain was a same-day triage of
# release-pipeline issues, not new rule shipping. So every shipped rule's
# canonical `since-version` is v0.5.0.
#
# Idempotent: re-running on an already-correct page is a no-op.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$docs = Join-Path $root 'docs\rules'
$rules = Join-Path $root 'core\src\rules'

# Match `since-version: <value>` in the YAML frontmatter; the value can be
# bare or quoted (`v0.3.0` vs `"v0.3.0"`). Replacement always emits the
# bare-token form for consistency with the rest of the frontmatter.
$sinceRe = '(?m)^since-version:\s*"?(v?[\d.]+)"?\s*$'

$rewritten = 0
$alreadyOk = 0
$skippedStub = 0

foreach ($md in Get-ChildItem -Path $docs -Filter '*.md' -File) {
    if ($md.Name -eq 'index.md' -or $md.Name -eq '_template.md') { continue }
    $text = [System.IO.File]::ReadAllText($md.FullName)
    if ($text -notmatch $sinceRe) { continue }
    $current = $matches[1]

    $base = $md.BaseName -replace '-', '_'
    $cpp = Join-Path $rules ($base + '.cpp')
    if (-not (Test-Path $cpp)) {
        $skippedStub++
        continue
    }
    if ($current -eq 'v0.5.0') {
        $alreadyOk++
        continue
    }
    $newText = [regex]::Replace($text, $sinceRe, 'since-version: v0.5.0')
    [System.IO.File]::WriteAllText($md.FullName, $newText)
    $rewritten++
}

Write-Output "rewritten: $rewritten"
Write-Output "already-ok: $alreadyOk"
Write-Output "skipped (still doc-only stubs): $skippedStub"
