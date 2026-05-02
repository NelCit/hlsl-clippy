# tools/b3_set_language_applicability.ps1
#
# ADR 0021 sub-phase B.3 (coarse — v1.4.0). For every rule doc page under
# `docs/rules/<rule>.md`, set the `language_applicability` front-matter
# field based on the rule's Stage::* declaration:
#
#   Stage::Ast / Stage::ControlFlow / Stage::Reflection / Stage::Ir
#       -> language_applicability: ["hlsl", "slang"]
#       (sub-phase B's empirical pass-through is ~99% for AST/CFG; reflection
#       was lit up in v1.3.1; conservative ["hlsl", "slang"] is appropriate
#       under "ship-what-works, stub-what-doesn't" — outliers demote in
#       v1.4.x via the fine-grained B.3 audit.)
#
#   Doc-only stubs (no matching .cpp file in core/src/rules/)
#       -> language_applicability: ["hlsl"]
#       (Conservative — these rules haven't shipped, so we don't pre-claim
#       Slang coverage for them. ADR 0021 §"v1.4.0 minimum-viable scope"
#       requires every rule's field be set, even if the value is `["hlsl"]`.)
#
# The script preserves any existing `language_applicability` values exactly
# (idempotent re-runs are no-ops on already-set rules).
#
# Run from repo root via:
#   pwsh tools\b3_set_language_applicability.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repo  = (Get-Location).Path
$Rules = Join-Path $Repo 'core\src\rules'
$Docs  = Join-Path $Repo 'docs\rules'

function Get-StageForRule([string]$RuleId) {
    $cppName = ($RuleId -replace '-', '_') + '.cpp'
    $cppPath = Join-Path $Rules $cppName
    if (-not (Test-Path $cppPath)) { return $null }
    $body = Get-Content -LiteralPath $cppPath -Raw
    foreach ($s in @('Ast', 'ControlFlow', 'Reflection', 'Ir')) {
        if ($body -match "return\s+Stage::$s\b") { return $s }
    }
    return $null
}

$updated   = 0
$skipped   = 0
$stubbed   = 0
$preserved = 0

foreach ($f in Get-ChildItem -Path $Docs -Filter '*.md') {
    if ($f.Name -eq '_template.md') { continue }
    $ruleId = $f.BaseName
    $body = Get-Content -LiteralPath $f.FullName -Raw

    if ($body -match '(?ms)^---\s*\r?\n.*?^language_applicability\s*:') {
        # Already set — preserve verbatim (idempotent).
        $preserved++
        continue
    }

    $stage = Get-StageForRule -RuleId $ruleId
    if ($null -eq $stage) {
        # Doc-only stub. Conservative ["hlsl"] lock.
        $applicability = '["hlsl"]'
        $stubbed++
    } else {
        # Shipped rule (any stage). Coarse ["hlsl", "slang"] under sub-phase B.
        $applicability = '["hlsl", "slang"]'
        $updated++
    }

    # Insert language_applicability into the YAML front-matter, just
    # before the closing `---`. The front-matter is delimited by `---`
    # lines at the top of the file.
    $newField = "language_applicability: $applicability"
    $rx = [regex]'(?ms)\A(---\s*\r?\n.*?)(\r?\n---\s*\r?\n)'
    $m = $rx.Match($body)
    if (-not $m.Success) {
        $skipped++
        continue
    }
    $head = $m.Groups[1].Value
    $tail = $m.Groups[2].Value
    $rest = $body.Substring($m.Length)
    $body2 = $head + "`n" + $newField + $tail + $rest

    Set-Content -LiteralPath $f.FullName -Value $body2 -NoNewline
}

Write-Host ""
Write-Host "B.3 coarse pass result:"
Write-Host ("  updated  (set to [hlsl, slang]): {0}" -f $updated)
Write-Host ("  stubbed  (doc-only -> [hlsl]):   {0}" -f $stubbed)
Write-Host ("  preserved (already had field):   {0}" -f $preserved)
Write-Host ("  skipped  (no front-matter):      {0}" -f $skipped)
