# tools/adoption-poll.ps1
#
# Polls the two external adoption metrics ADR 0018 §5 criteria #7 + #8
# track for the v1.1 readiness review (per ADR 0019 §"v1.x patch
# trajectory"):
#
#   #7 — VS Code Marketplace install count for `nelcit.shader-clippy`
#        (target: >= 5,000).
#   #8 — Downstream integrations: number of public GitHub repos that
#        reference shader-clippy from a workflow file (target: >= 5).
#
# This script does NOT validate either threshold — the maintainer reviews
# the trend periodically (suggested cadence: monthly). Each invocation
# appends one dated row to `docs/adoption-metrics.md` (created on first
# run).
#
# Dependencies:
#   - `vsce` (Node CLI; `npm i -g @vscode/vsce`). vsce show --json prints
#     a JSON object with installs, rating, version, lastUpdated.
#   - `gh` (GitHub CLI; `winget install GitHub.cli` or equivalent).
#     Authenticated with `gh auth login`.
#
# Both tools are best-effort: if either is missing or fails, the script
# logs the failure and writes a sentinel value into the row rather than
# aborting. Partial data is better than no data on a recurring poll.
#
# Usage (from repo root):
#   pwsh tools\adoption-poll.ps1
#   pwsh tools\adoption-poll.ps1 -DryRun     # print, do not append

[CmdletBinding()]
param(
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
$OutputMd  = Join-Path $RepoRoot 'docs\adoption-metrics.md'

# --- 1. Marketplace listing ----------------------------------------------

$installs = '?'
$rating   = '?'
$version  = '?'
$mpUpdated = '?'

$vsceCmd = Get-Command vsce -ErrorAction SilentlyContinue
if (-not $vsceCmd) {
    Write-Warning "adoption-poll: 'vsce' not on PATH. Install with 'npm i -g @vscode/vsce'."
} else {
    try {
        # `vsce show <publisher>.<name> --json` returns a JSON object with
        # `statistics`: an array of { statisticName; value } pairs.
        $raw = & vsce show 'nelcit.shader-clippy' --json 2>$null
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($raw)) {
            Write-Warning "adoption-poll: 'vsce show' returned exit $LASTEXITCODE; using sentinels."
        } else {
            $info = $raw | ConvertFrom-Json
            if ($info.PSObject.Properties['versions']) {
                $latest = $info.versions | Select-Object -First 1
                if ($latest -and $latest.PSObject.Properties['version']) {
                    $version = $latest.version
                }
                if ($latest -and $latest.PSObject.Properties['lastUpdated']) {
                    $mpUpdated = $latest.lastUpdated
                }
            }
            if ($info.PSObject.Properties['statistics']) {
                foreach ($s in $info.statistics) {
                    switch ($s.statisticName) {
                        'install'        { $installs = [string]$s.value }
                        'averagerating'  { $rating   = [string]$s.value }
                    }
                }
            }
        }
    } catch {
        Write-Warning "adoption-poll: vsce show failed: $($_.Exception.Message)"
    }
}

# --- 2. Downstream integrations -------------------------------------------
#
# We use GitHub code search to count public repos whose workflow files
# (or `.shader-clippy.toml` configs) mention `shader-clippy`. Code search
# excludes forks by default. The `--limit 100` cap is a backstop in case
# the result set explodes; we count via `--json repository` so duplicates
# from multiple matches in the same repo collapse on dedup.

$downstreamCount = '?'
$downstreamRepos = @()

$ghCmd = Get-Command gh -ErrorAction SilentlyContinue
if (-not $ghCmd) {
    Write-Warning "adoption-poll: 'gh' not on PATH. Install GitHub CLI + 'gh auth login'."
} else {
    try {
        # We deliberately exclude this repo (NelCit/shader-clippy) from the
        # downstream count. `gh search code` does not support `-repo:`
        # exclusion directly; we filter client-side after dedup.
        $jsonRaw = & gh search code 'shader-clippy filename:.github/workflows' `
            --json repository --limit 100 2>$null
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($jsonRaw)) {
            Write-Warning "adoption-poll: 'gh search code' returned exit $LASTEXITCODE."
        } else {
            $hits = $jsonRaw | ConvertFrom-Json
            $repoSet = New-Object System.Collections.Generic.HashSet[string]
            foreach ($h in $hits) {
                if (-not $h.PSObject.Properties['repository']) { continue }
                $repo = $h.repository
                $name = $null
                if ($repo.PSObject.Properties['nameWithOwner']) {
                    $name = $repo.nameWithOwner
                } elseif ($repo.PSObject.Properties['fullName']) {
                    $name = $repo.fullName
                }
                if (-not $name) { continue }
                if ($name -ieq 'NelCit/shader-clippy') { continue }
                [void]$repoSet.Add($name)
            }
            $downstreamRepos = @($repoSet)
            $downstreamCount = [string]$repoSet.Count
        }
    } catch {
        Write-Warning "adoption-poll: gh search failed: $($_.Exception.Message)"
    }
}

# --- 3. Render row + append (or create) ---------------------------------

$today = (Get-Date).ToString('yyyy-MM-dd')
$repoList = ''
if ($downstreamRepos.Count -gt 0) {
    $sorted = $downstreamRepos | Sort-Object
    if ($sorted.Count -gt 5) {
        $repoList = ($sorted[0..4] -join ', ') + (', + {0} more' -f ($sorted.Count - 5))
    } else {
        $repoList = $sorted -join ', '
    }
}

$row = "| $today | $installs | $rating | $version | $mpUpdated | $downstreamCount | $repoList |"

Write-Host "adoption-poll: $row"

if ($DryRun) {
    Write-Host "adoption-poll: -DryRun set; not appending to $OutputMd"
    return
}

if (-not (Test-Path -LiteralPath $OutputMd)) {
    $header = @'
# Adoption metrics — `nelcit.shader-clippy`

External adoption signals tracked for ADR 0018 §5 criteria #7
(Marketplace install count >= 5,000) and #8 (>= 5 downstream
integrations). These metrics were deferred from v1.0 to v1.1 readiness
review (per [ADR 0019](decisions/0019-v1-release-plan.md) §"v1.x patch
trajectory"); both targets remain non-blocking but tracked.

This file is appended to by `pwsh tools/adoption-poll.ps1` (or
`bash tools/adoption-poll.sh`); the v1.1.x cadence is monthly. The
script writes one dated row per invocation; rows are never overwritten.
The two thresholds (5,000 / 5) are not validated by the script — the
maintainer reviews the trend at the next release.

| Date | MP installs | MP rating | MP version | MP last-updated | Downstream repos | Top repos |
|------|-------------|-----------|------------|-----------------|------------------|-----------|
'@
    Set-Content -LiteralPath $OutputMd -Value $header -Encoding UTF8
}

Add-Content -LiteralPath $OutputMd -Value $row -Encoding UTF8
Write-Host "adoption-poll: appended row to $OutputMd"
