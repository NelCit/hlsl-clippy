# tools/b4_passthrough_probe.ps1
#
# ADR 0021 sub-phase B.4 empirical pass-through probe. For each .hlsl
# fixture under tests/fixtures/{phase2,phase3,phase4,phase7,phase8}/,
# create a `.slang`-extensioned copy, lint both, and diff the rule-id
# sets. Reports:
#   - HLSL distinct rule ids fired
#   - Slang distinct rule ids fired
#   - Slang ∩ HLSL    (pass-through count)
#   - HLSL \ Slang    (rules that fire on .hlsl but not .slang)
#   - Slang \ HLSL    (rules that fire on .slang but not .hlsl, sanity)
#
# Run from repo root via:
#   pwsh tools\b4_passthrough_probe.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Repo = (Get-Location).Path
$Cli  = Join-Path $Repo 'build\cli\hlsl-clippy.exe'
if (-not (Test-Path $Cli)) {
    Write-Host "build CLI not found at $Cli; run cmake --build build --target hlsl-clippy first."
    exit 1
}

$Phases = @('phase2', 'phase3', 'phase4', 'phase7', 'phase8')
$tmpDir = Join-Path $Repo 'build\b4_probe'
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

function Get-RuleSet([string]$Path) {
    $out = & $Cli lint $Path 2>&1 | Out-String
    $rx  = [regex]'\[([a-z][a-z0-9-]+)\]'
    $matches = $rx.Matches($out)
    $set = @{}
    foreach ($m in $matches) { $set[$m.Groups[1].Value] = $true }
    return $set
}

$totalHlsl  = @{}
$totalSlang = @{}
$totalIntersect = @{}

foreach ($phase in $Phases) {
    $phaseDir = Join-Path $Repo "tests\fixtures\$phase"
    if (-not (Test-Path $phaseDir)) { continue }
    foreach ($f in Get-ChildItem -Path $phaseDir -Filter '*.hlsl') {
        $slangCopy = Join-Path $tmpDir ($f.BaseName + '.slang')
        Copy-Item -Path $f.FullName -Destination $slangCopy -Force

        $hlslSet  = Get-RuleSet -Path $f.FullName
        $slangSet = Get-RuleSet -Path $slangCopy

        foreach ($k in $hlslSet.Keys)  { $totalHlsl[$k]  = $true }
        foreach ($k in $slangSet.Keys) { $totalSlang[$k] = $true }
        foreach ($k in $hlslSet.Keys)  {
            if ($slangSet.ContainsKey($k)) { $totalIntersect[$k] = $true }
        }
    }
}

$hlslOnly  = @($totalHlsl.Keys  | Where-Object { -not $totalSlang.ContainsKey($_) })
$slangOnly = @($totalSlang.Keys | Where-Object { -not $totalHlsl.ContainsKey($_)  })

Write-Host ""
Write-Host "B.4 empirical pass-through probe across phase{2,3,4,7,8} fixtures:"
Write-Host ("  HLSL distinct rule ids fired:    {0}" -f $totalHlsl.Count)
Write-Host ("  Slang distinct rule ids fired:   {0}" -f $totalSlang.Count)
Write-Host ("  Slang INTERSECT HLSL:            {0}" -f $totalIntersect.Count)
Write-Host ("  HLSL-only (regression?):         {0}" -f $hlslOnly.Count)
Write-Host ("  Slang-only (sanity):             {0}" -f $slangOnly.Count)
if ($hlslOnly.Count -gt 0) {
    Write-Host ""
    Write-Host "Rules firing on .hlsl but NOT on .slang:"
    foreach ($r in ($hlslOnly | Sort-Object)) { Write-Host "  - $r" }
}
if ($slangOnly.Count -gt 0) {
    Write-Host ""
    Write-Host "Rules firing on .slang but NOT on .hlsl (unusual):"
    foreach ($r in ($slangOnly | Sort-Object)) { Write-Host "  - $r" }
}
