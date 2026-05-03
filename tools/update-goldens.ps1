# tools/update-goldens.ps1 -- regenerate golden-snapshot JSON.
#
# Wraps `cmake --build` against the CLI target, then runs the CLI on every
# fixture under `tests/golden/fixtures/` and normalises the plain-text
# output into canonical sorted JSON under `tests/golden/snapshots/`.
#
# The C++ test driver `tests/unit/test_golden_snapshots.cpp` produces the
# same canonical bytes via the C++ API; the two paths must agree.
#
# Usage:
#   pwsh tools/update-goldens.ps1                      # default (.\build)
#   pwsh tools/update-goldens.ps1 -BuildDir build-rel  # explicit build dir
#   pwsh tools/update-goldens.ps1 -SkipBuild           # skip cmake --build

[CmdletBinding()]
param(
    [string] $BuildDir = "build",
    [switch] $SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$fixturesDir = Join-Path $repoRoot "tests/golden/fixtures"
$snapshotsDir = Join-Path $repoRoot "tests/golden/snapshots"
$cliExe = Join-Path $repoRoot "$BuildDir/cli/shader-clippy.exe"

if (-not $SkipBuild) {
    Write-Host "[update-goldens] cmake --build $BuildDir --target shader-clippy"
    & cmake --build $BuildDir --target shader-clippy
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build failed (exit $LASTEXITCODE)"
    }
}

if (-not (Test-Path $cliExe)) {
    throw "CLI not found: $cliExe (build first or pass -BuildDir)"
}
if (-not (Test-Path $fixturesDir)) {
    throw "fixtures dir not found: $fixturesDir"
}
New-Item -ItemType Directory -Force -Path $snapshotsDir | Out-Null

# CLI plain-text header line:
#   <path>:<line>:<col>: <severity>: <message> [<rule-id>]
# Rule id is the LAST `[...]` group on the line; messages don't end in `]`.
$headerRe = '^(?<path>.+?):(?<line>\d+):(?<col>\d+):\s+(?<severity>error|warning|note):\s+(?<message>.+)\s+\[(?<rule>[A-Za-z0-9_\-:]+)\]\s*$'

# Hand-roll a JSON serialiser that matches `nlohmann::json::dump(2)` exactly:
# - 2-space indent
# - `: ` separator (single space)
# - UTF-8 passes through (ensure_ascii=false default)
# - escape only `"`, `\`, and JSON-mandatory control chars
# - object keys preserved in insertion order
function Format-JsonString {
    param([string] $s)
    $sb = New-Object System.Text.StringBuilder
    [void] $sb.Append('"')
    foreach ($ch in $s.ToCharArray()) {
        $code = [int] $ch
        switch ($code) {
            0x22 { [void] $sb.Append('\"'); break }
            0x5C { [void] $sb.Append('\\'); break }
            0x08 { [void] $sb.Append('\b'); break }
            0x0C { [void] $sb.Append('\f'); break }
            0x0A { [void] $sb.Append('\n'); break }
            0x0D { [void] $sb.Append('\r'); break }
            0x09 { [void] $sb.Append('\t'); break }
            default {
                if ($code -lt 0x20) {
                    [void] $sb.Append(('\u{0:x4}' -f $code))
                } else {
                    [void] $sb.Append($ch)
                }
            }
        }
    }
    [void] $sb.Append('"')
    return $sb.ToString()
}

function Format-JsonValue {
    param($value, [int] $indent)
    $pad = ' ' * ($indent * 2)
    $childPad = ' ' * (($indent + 1) * 2)

    if ($null -eq $value) { return 'null' }
    if ($value -is [bool]) { if ($value) { return 'true' } else { return 'false' } }
    if ($value -is [int] -or $value -is [long] -or $value -is [double] -or $value -is [decimal]) {
        return [string] $value
    }
    if ($value -is [string]) {
        return Format-JsonString -s $value
    }
    if ($value -is [System.Collections.IDictionary]) {
        $keys = @($value.Keys)
        if ($keys.Count -eq 0) { return '{}' }
        $parts = New-Object System.Collections.ArrayList
        foreach ($k in $keys) {
            $kj = Format-JsonString -s ([string] $k)
            $vj = Format-JsonValue -value $value[$k] -indent ($indent + 1)
            [void] $parts.Add("$childPad$kj`: $vj")
        }
        return "{`n" + ($parts -join ",`n") + "`n$pad}"
    }
    if ($value -is [System.Collections.IEnumerable]) {
        $arr = @($value)
        if ($arr.Count -eq 0) { return '[]' }
        $parts = New-Object System.Collections.ArrayList
        foreach ($item in $arr) {
            $vj = Format-JsonValue -value $item -indent ($indent + 1)
            [void] $parts.Add("$childPad$vj")
        }
        return "[`n" + ($parts -join ",`n") + "`n$pad]"
    }
    # Fallback: stringify.
    return Format-JsonString -s ([string] $value)
}

$fixtures = Get-ChildItem -Path $fixturesDir -Filter '*.hlsl' | Sort-Object Name
if ($fixtures.Count -eq 0) {
    throw "no fixtures in $fixturesDir"
}

$totalDiags = 0
foreach ($fixture in $fixtures) {
    # Capture stdout; CLI exits non-zero when diagnostics fire (that's fine).
    $output = & $cliExe lint $fixture.FullName 2>$null
    $rows = New-Object System.Collections.ArrayList
    foreach ($line in ($output -split "`r?`n")) {
        if ($line -match $headerRe) {
            $row = [ordered]@{
                rule     = $Matches['rule']
                line     = [int] $Matches['line']
                col      = [int] $Matches['col']
                severity = $Matches['severity']
                message  = $Matches['message'].TrimEnd()
            }
            [void] $rows.Add($row)
        }
    }
    # Sort by (line, col, rule). PowerShell's Sort-Object with multiple
    # property expressions; capture into an array.
    $sorted = @($rows | Sort-Object -Property `
        @{Expression = { $_.line }; Ascending = $true },
        @{Expression = { $_.col }; Ascending = $true },
        @{Expression = { $_.rule }; Ascending = $true })

    $obj = [ordered]@{
        fixture     = $fixture.Name
        diagnostics = $sorted
    }

    $json = Format-JsonValue -value $obj -indent 0
    $json += "`n"

    $outPath = Join-Path $snapshotsDir ($fixture.BaseName + ".json")
    # UTF-8 without BOM, LF line endings.
    [System.IO.File]::WriteAllText($outPath, $json, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "  $($fixture.Name): $($sorted.Count) diagnostic(s) -> $($fixture.BaseName).json"
    $totalDiags += $sorted.Count
}

Write-Host "[update-goldens] wrote $($fixtures.Count) snapshot(s); $totalDiags total diagnostics"
