# tools/release-audit.ps1
#
# Pre-tag release-readiness audit (ADR 0018 §5 criterion #12). PowerShell
# 5.1-compatible mirror of `tools/release-audit.sh`.
#
# Walks six checks against the working tree + git history:
#
#   1. DCO          — every commit between the previous tag and HEAD
#                     carries a `Signed-off-by:` trailer matching the
#                     author email.
#   2. Conventional — every commit subject matches one of the standard
#      Commits        types (feat / fix / refactor / docs / chore / test /
#                     build / ci / release), with optional `(scope)`.
#   3. CHANGELOG    — `## [<version>]` heading exists for the new tag.
#   4. Version sync — `core/src/version.cpp`, `vscode-extension/package.json`,
#                     the new `CHANGELOG.md` heading, and the supplied
#                     tag version all agree.
#   5. ADR index    — every `Accepted` ADR file under `docs/decisions/`
#                     appears as an `Accepted` row in the CLAUDE.md table.
#   6. Public hdrs  — every file under `core/include/hlsl_clippy/` has
#                     `#pragma once` and ends in `.hpp`.
#
# Usage:
#   pwsh tools\release-audit.ps1 -TagVersion 1.0.0
#
# Exits 0 when all checks pass, 1 otherwise. Per-check pass/fail is
# printed even when later checks short-circuit.
#
# Targets PowerShell 5.1 (the default Windows shell) — no PS7-only
# syntax (no `&&` / `||`, no ternary, no null-coalescing, no `?.`).

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$TagVersion
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Locate repo root ------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir

# --- Per-check tally -------------------------------------------------------
$script:PassCount = 0
$script:FailCount = 0

function Pass([string]$Message) {
    Write-Host "  PASS - $Message"
    $script:PassCount++
}

function Fail([string]$Message) {
    Write-Host "  FAIL - $Message"
    $script:FailCount++
}

# --- Resolve previous tag --------------------------------------------------
# `git describe --abbrev=0 --tags` resolves the latest tag. On a fresh
# repo we fall back to the root commit so the `..HEAD` range is well-
# defined.
$prevTag = $null
try {
    $prevTag = (& git -C $RepoRoot describe --abbrev=0 --tags 2>$null)
    if ($LASTEXITCODE -ne 0) { $prevTag = $null }
} catch {
    $prevTag = $null
}

if ([string]::IsNullOrWhiteSpace($prevTag)) {
    $prevRef = (& git -C $RepoRoot rev-list --max-parents=0 HEAD).Split("`n")[0].Trim()
    $prevLabel = '<root>'
} else {
    $prevRef = $prevTag.Trim()
    $prevLabel = $prevTag.Trim()
}

Write-Host "release-audit: tag-to-be     = v$TagVersion"
Write-Host "release-audit: previous tag  = $prevLabel"
Write-Host "release-audit: repo root     = $RepoRoot"
Write-Host ""

# Pull the commit list once — both DCO and Conventional Commits checks
# walk the same range.
$rawLog = & git -C $RepoRoot log "$prevRef..HEAD" '--format=%H%x09%ae%x09%s%x09%B%x1e' 2>$null
if (-not $rawLog) { $rawLog = '' }
# Records separated by 0x1e (record separator). Each record's fields are
# separated by tab: SHA, author-email, subject, full-body.
$records = @()
foreach ($chunk in ($rawLog -split [char]0x1e)) {
    $trimmed = $chunk.TrimStart("`r","`n")
    if ([string]::IsNullOrWhiteSpace($trimmed)) { continue }
    $parts = $trimmed.Split([char]0x09, 4)
    if ($parts.Count -lt 4) { continue }
    $records += [pscustomobject]@{
        Sha     = $parts[0]
        Email   = $parts[1]
        Subject = $parts[2]
        Body    = $parts[3]
    }
}

# ──────────────────────────────────────────────────────────────────────────
# Check 1 — DCO sign-off on every commit since the previous tag.
# ──────────────────────────────────────────────────────────────────────────
Write-Host "[1/6] DCO sign-off (every commit since $prevLabel)"
$dcoFails = 0
foreach ($r in $records) {
    # Use `git interpret-trailers --parse` to extract Signed-off-by trailers
    # from the commit body. The previous regex-on-the-body approach was
    # fragile against line-ending variations (CRLF on Windows, mixed
    # endings in the git-log output) and would false-fail on commits with
    # the trailer present. `interpret-trailers` is the canonical parser.
    $trailers = & git -C $RepoRoot show -s --format=%B $r.Sha 2>$null |
        & git -C $RepoRoot interpret-trailers --parse 2>$null
    if (-not $trailers) { $trailers = '' }
    $expected = "<$($r.Email)>"
    $found = $false
    foreach ($line in ($trailers -split "`n")) {
        if ($line -match '^Signed-off-by:' -and $line -like "*$expected*") {
            $found = $true
            break
        }
    }
    if ($found) { continue }
    Write-Host "    missing/mismatched sign-off: $($r.Sha.Substring(0,12)) by $($r.Email)"
    $dcoFails++
}
if ($dcoFails -eq 0) {
    Pass "every commit since $prevLabel carries a matching Signed-off-by"
} else {
    Fail "$dcoFails commit(s) since $prevLabel are missing a matching Signed-off-by"
}

# ──────────────────────────────────────────────────────────────────────────
# Check 2 — Conventional Commits on every commit since the previous tag.
# ──────────────────────────────────────────────────────────────────────────
Write-Host "[2/6] Conventional Commits subject lines"
$ccPattern = '^(feat|fix|refactor|docs|chore|test|build|ci|release)(\([^)]+\))?!?:\s.+'
$ccFails = 0
foreach ($r in $records) {
    if ($r.Subject -match $ccPattern) { continue }
    Write-Host "    non-conformant subject: $($r.Sha.Substring(0,12)) - `"$($r.Subject)`""
    $ccFails++
}
if ($ccFails -eq 0) {
    Pass "every commit subject since $prevLabel matches Conventional Commits"
} else {
    Fail "$ccFails commit subject(s) since $prevLabel do not match Conventional Commits"
}

# ──────────────────────────────────────────────────────────────────────────
# Check 3 — CHANGELOG.md has a `## [<version>]` heading for the new tag.
# ──────────────────────────────────────────────────────────────────────────
Write-Host "[3/6] CHANGELOG entry for $TagVersion"
$changelogPath = Join-Path $RepoRoot 'CHANGELOG.md'
if (-not (Test-Path -LiteralPath $changelogPath)) {
    Fail "CHANGELOG.md is missing at repo root"
} else {
    $content = Get-Content -LiteralPath $changelogPath -Raw
    $expectedHeading = '^## \[' + [regex]::Escape($TagVersion) + '\]'
    if ([regex]::IsMatch($content, $expectedHeading, 'Multiline')) {
        Pass "CHANGELOG.md has '## [$TagVersion]' heading"
    } else {
        Fail "CHANGELOG.md has no '## [$TagVersion]' heading"
    }
}

# ──────────────────────────────────────────────────────────────────────────
# Check 4 — version strings match across all canonical sources + tag.
# ──────────────────────────────────────────────────────────────────────────
Write-Host "[4/6] Version strings cross-check"
$coreVersionFile = Join-Path $RepoRoot 'core\src\version.cpp'
$vscPkgFile      = Join-Path $RepoRoot 'vscode-extension\package.json'

$coreVer = ''
if (Test-Path -LiteralPath $coreVersionFile) {
    $m = [regex]::Match((Get-Content -LiteralPath $coreVersionFile -Raw), 'return\s+"([^"]+)"')
    if ($m.Success) { $coreVer = $m.Groups[1].Value }
}

$vscVer = ''
if (Test-Path -LiteralPath $vscPkgFile) {
    $m = [regex]::Match((Get-Content -LiteralPath $vscPkgFile -Raw), '"version"\s*:\s*"([^"]+)"')
    if ($m.Success) { $vscVer = $m.Groups[1].Value }
}

$changelogVer = ''
if (Test-Path -LiteralPath $changelogPath) {
    $m = [regex]::Match((Get-Content -LiteralPath $changelogPath -Raw),
        '^## \[([0-9][^\]]*)\]', 'Multiline')
    if ($m.Success) { $changelogVer = $m.Groups[1].Value }
}

$mismatches = 0
function Report-Version([string]$Label, [string]$Actual) {
    if ($Actual -eq $TagVersion) {
        Write-Host ('    {0,-32} = {1} (match)' -f $Label, $Actual)
    } else {
        $shown = if ([string]::IsNullOrEmpty($Actual)) { '<missing>' } else { $Actual }
        Write-Host ('    {0,-32} = {1} (expected {2})' -f $Label, $shown, $TagVersion)
        $script:Check4Fails++
    }
}
$script:Check4Fails = 0
Report-Version 'core/src/version.cpp'            $coreVer
Report-Version 'vscode-extension/package.json'   $vscVer
Report-Version 'CHANGELOG.md latest heading'     $changelogVer
if ($script:Check4Fails -eq 0) {
    Pass "all four version sources agree on $TagVersion"
} else {
    Fail "$script:Check4Fails version source(s) do not match $TagVersion"
}

# ──────────────────────────────────────────────────────────────────────────
# Check 5 — every Accepted ADR file is in the CLAUDE.md table as Accepted.
# ──────────────────────────────────────────────────────────────────────────
Write-Host "[5/6] ADR index sync (CLAUDE.md <-> docs/decisions/)"
$decisionsDir = Join-Path $RepoRoot 'docs\decisions'
$claudeMdPath = Join-Path $RepoRoot 'CLAUDE.md'
$adrFails = 0
if (-not (Test-Path -LiteralPath $decisionsDir) -or
    -not (Test-Path -LiteralPath $claudeMdPath)) {
    Fail "missing $decisionsDir or $claudeMdPath"
} else {
    $claudeContent = Get-Content -LiteralPath $claudeMdPath -Raw
    # Win32 file-system filters do not support character classes, so we
    # filter on a regex post-hoc. The 4-digit numeric prefix ensures we
    # pick up only ADR files (`0001-...md`), not `README.md` or
    # subdirectories.
    $adrFiles = Get-ChildItem -LiteralPath $decisionsDir -File |
        Where-Object { $_.Name -match '^[0-9]{4}-.*\.md$' }
    foreach ($adr in $adrFiles) {
        $raw = Get-Content -LiteralPath $adr.FullName -Raw
        $statusMatch = [regex]::Match($raw, '(?ms)^---\s*\r?\n.*?^status:\s*([^\r\n]+)')
        if (-not $statusMatch.Success) { continue }
        $status = $statusMatch.Groups[1].Value.Trim().Trim('"').Trim("'")
        if ($status -ne 'Accepted') { continue }

        # Match on the basename (anywhere on a line) plus `Accepted` on
        # the same line. CLAUDE.md's ADR-index table currently spells
        # the link as `[NNNN](docs/decisions/<basename>) | ... | Accepted`.
        # Walk lines individually -- a multi-line `[regex]::IsMatch`
        # would let `.*` cross newlines under some regex options and
        # mis-match unrelated rows.
        $found = $false
        foreach ($line in ($claudeContent -split "`r?`n")) {
            if ($line.Contains($adr.Name) -and $line.Contains('Accepted')) {
                $found = $true
                break
            }
        }
        if ($found) { continue }

        Write-Host "    $($adr.Name) is Accepted but not listed Accepted in CLAUDE.md"
        $adrFails++
    }
    if ($adrFails -eq 0) {
        Pass "every Accepted ADR appears as Accepted in CLAUDE.md"
    } else {
        Fail "$adrFails Accepted ADR(s) missing from CLAUDE.md or marked otherwise"
    }
}

# ──────────────────────────────────────────────────────────────────────────
# Check 6 — every public header has `#pragma once` and ends in `.hpp`.
# ──────────────────────────────────────────────────────────────────────────
Write-Host "[6/6] Public-header guard (core/include/hlsl_clippy/)"
$headersDir = Join-Path $RepoRoot 'core\include\hlsl_clippy'
$headerFails = 0
if (-not (Test-Path -LiteralPath $headersDir)) {
    Fail "missing $headersDir"
} else {
    $entries = Get-ChildItem -LiteralPath $headersDir -File
    foreach ($entry in $entries) {
        if ($entry.Extension -ne '.hpp') {
            $rel = $entry.FullName.Substring($RepoRoot.Length + 1)
            Write-Host "    $rel does not end in .hpp"
            $headerFails++
            continue
        }
        $body = Get-Content -LiteralPath $entry.FullName -Raw
        if (-not [regex]::IsMatch($body, '^\s*#pragma\s+once', 'Multiline')) {
            $rel = $entry.FullName.Substring($RepoRoot.Length + 1)
            Write-Host "    $rel has no '#pragma once'"
            $headerFails++
        }
    }
    if ($headerFails -eq 0) {
        Pass "every file under core/include/hlsl_clippy/ ends in .hpp + has #pragma once"
    } else {
        Fail "$headerFails public-header guard violation(s)"
    }
}

# --- Summary ---------------------------------------------------------------
$total = $script:PassCount + $script:FailCount
Write-Host ""
Write-Host "release-audit: $script:PassCount/$total checks passed"
if ($script:FailCount -gt 0) {
    Write-Host "release-audit: NOT READY to tag v$TagVersion"
    exit 1
}
Write-Host "release-audit: READY to tag v$TagVersion"
exit 0
