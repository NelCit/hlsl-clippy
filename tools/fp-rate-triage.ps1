# tools/fp-rate-triage.ps1
#
# Deterministic per-rule TP/FP triage pass over `tests/corpus/FP_RATES.md`,
# implementing the v1.1 ship list from ADR 0019 §"v1.x patch trajectory":
# "full FP-rate triage". The companion script `tools/fp-rate-baseline.ps1`
# generates the firing-count table with the `Triage` column initialised
# to TODO; this script applies a deterministic decision procedure to
# replace TODO entries with TP / FP / MIXED / NEEDS-HUMAN classifications.
#
# Decision procedure (applied in order, per row):
#
#   1. TP-default — the rule's natural domain matches the corpus shader
#      stages where it fired. Mapping is encoded in $RuleNaturalDomains
#      below. E.g. `groupshared-*` rules firing on `compute/` shaders
#      are TP. The output is the literal string `TP`.
#
#   2. FP-default — the rule's detection is heuristic and the corpus
#      shaders sit in a domain where the heuristic is known to overfire.
#      E.g. `vgpr-pressure-warning` firing on tiny vertex shaders is
#      flagged FP because the static estimate is unreliable below ~30
#      live registers. Encoded in $RuleHeuristicOverfire.
#
#   3. MIXED — the rule fires across both natural-domain shaders and
#      out-of-domain shaders, or the firings split TP/FP within a single
#      shader. Output is `<n> TP / <m> FP` if computable, otherwise the
#      literal string `MIXED -- needs human review`.
#
#   4. NEEDS-HUMAN — none of the above resolves. The script emits the
#      literal `NEEDS-HUMAN` and a one-line note alongside.
#
# Reproducibility: re-running this script PRESERVES rows that the
# maintainer manually edited (i.e. anything other than `TODO` in the
# Triage column). Only `TODO` rows are updated. This is the "still-TODO"
# guard the v1.1 spec calls out.
#
# FP rate per rule = FP / (TP + FP). Rules with FP rate > 5% land in a
# new "Above-budget rules" section at the top of FP_RATES.md.
#
# Usage (from repo root):
#   pwsh tools\fp-rate-triage.ps1
#   pwsh tools\fp-rate-triage.ps1 -DryRun       # print but do not write

[CmdletBinding()]
param(
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Force invariant culture for `{0:N1}` percentage formatting so non-US
# locales don't emit commas (e.g. "25,0%" instead of "25.0%"). Without
# this, regenerating the file on a fr-FR developer box produces a diff
# that breaks downstream parsers.
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot   = Split-Path -Parent $ScriptDir
$OutputMd   = Join-Path $RepoRoot 'tests\corpus\FP_RATES.md'

if (-not (Test-Path -LiteralPath $OutputMd)) {
    Write-Error "fp-rate-triage: $OutputMd not found. Run fp-rate-baseline.ps1 first."
    exit 1
}

# --- Decision tables ------------------------------------------------------
#
# $RuleNaturalDomains: rule-id -> list of corpus-shader path prefixes
# (relative to tests/corpus/) where the rule's detection is well-founded.
# A rule firing on a shader whose path begins with one of its prefixes is
# TP-default. The prefixes follow the corpus directory layout:
# amplification/, compute/, mesh/, pixel/, raytracing/, vertex/.
#
# Rules absent from this map default to NEEDS-HUMAN unless they appear
# in $RuleHeuristicOverfire (FP) below.

$RuleNaturalDomains = @{
    # Workgroup / LDS / barriers — strictly compute (and amplification +
    # mesh, which both run a workgroup and may use groupshared).
    'groupshared-first-read-without-barrier'    = @('compute/', 'amplification/', 'mesh/')
    'groupshared-write-then-no-barrier-read'    = @('compute/', 'amplification/', 'mesh/')
    'groupshared-overwrite-before-barrier'      = @('compute/', 'amplification/', 'mesh/')
    'groupshared-too-large'                     = @('compute/', 'amplification/', 'mesh/')
    'groupshared-atomic-replaceable-by-wave'    = @('compute/', 'amplification/', 'mesh/')
    'interlocked-bin-without-wave-prereduce'    = @('compute/', 'amplification/', 'mesh/')
    'wave-prefix-sum-vs-scan-with-atomics'      = @('compute/', 'amplification/', 'mesh/')
    'manual-wave-reduction-pattern'             = @('compute/', 'amplification/', 'mesh/', 'pixel/')
    'scratch-from-dynamic-indexing'             = @('compute/', 'amplification/', 'mesh/')

    # Bindings — apply broadly across all stages.
    'all-resources-bound-not-set'               = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'cbuffer-fits-rootconstants'                = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'cbuffer-large-fits-rootcbv-not-table'      = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'cbuffer-padding-hole'                      = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'structured-buffer-stride-mismatch'         = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'structured-buffer-stride-not-cache-aligned' = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'rwbuffer-store-without-globallycoherent'   = @('compute/', 'amplification/', 'mesh/')
    'static-sampler-when-dynamic-used'          = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'nointerpolation-mismatch'                  = @('vertex/', 'pixel/', 'mesh/')
    # PCF arithmetic shows up in pixel shaders but also in DXR closest-hit
    # / miss shaders that sample shadow maps for secondary lighting.
    'missing-precise-on-pcf'                    = @('pixel/', 'raytracing/')
    # Divergent indexing is a hazard in any stage with multiple lanes,
    # which is every stage on modern HW (vertex shaders run in waves too).
    'divergent-buffer-index-on-uniform-resource' = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')

    # Math simplifications — apply broadly; these are AST-only safe rewrites.
    'pow-const-squared'                         = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'pow-integer-decomposition'                 = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'inv-sqrt-to-rsqrt'                         = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'mul-identity'                              = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'manual-mad-decomposition'                  = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'div-without-epsilon'                       = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'compare-equal-float'                       = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')

    # Texture / sampler.
    'samplelevel-with-zero-on-mipped-tex'       = @('pixel/', 'compute/', 'mesh/', 'amplification/')

    # Control flow.
    'branch-on-uniform-missing-attribute'       = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'small-loop-no-unroll'                      = @('amplification/', 'compute/', 'mesh/', 'pixel/', 'raytracing/', 'vertex/')
    'wave-intrinsic-non-uniform'                = @('compute/', 'pixel/', 'amplification/', 'mesh/')

    # DXR / raytracing.
    'missing-ray-flag-cull-non-opaque'          = @('raytracing/')
    'recursion-depth-not-declared'              = @('raytracing/')
    'live-state-across-traceray'                = @('raytracing/')

    # Mesh-shader / sampling.
    'sample-use-no-interleave'                  = @('compute/', 'pixel/', 'mesh/', 'amplification/')

    # Work graphs.
    'nodeid-implicit-mismatch'                  = @('compute/')
}

# Rules where the heuristic is known to overfire on small shaders, and
# the corpus shaders that triggered it are likely below the rule's
# real-world signal threshold.
$RuleHeuristicOverfire = @{
    'vgpr-pressure-warning'                     = 'static estimate unreliable on tiny shaders'
}

# --- Parse the existing FP_RATES.md ---------------------------------------

$rawLines  = Get-Content -LiteralPath $OutputMd -Encoding UTF8

# Strip any previously-written "## Above-budget rules" block + its
# accompanying TP/FP/FP-rate table BEFORE the per-rule firing table is
# parsed. Without this strip the previous run's table ends up bundled
# into $tableRows below and we end up emitting two copies.
#
# The block runs from the heading until the next `## ` heading or the
# `| Rule | Total firings |` table header (whichever comes first).
$lines = New-Object System.Collections.ArrayList
$skipping = $false
foreach ($ln in $rawLines) {
    if (-not $skipping -and $ln -match '^##\s+Above-budget rules') {
        $skipping = $true
        continue
    }
    if ($skipping) {
        if ($ln -match '^##\s' -or $ln -match '^\|\s*Rule\s*\|\s*Total firings') {
            $skipping = $false
            [void]$lines.Add($ln)
            continue
        }
        continue
    }
    [void]$lines.Add($ln)
}
$preamble  = New-Object System.Collections.ArrayList
$tableRows = New-Object System.Collections.ArrayList
$footer    = New-Object System.Collections.ArrayList

# Three modes: 'pre' (lines before the table), 'tab' (table rows), 'post'
# (lines after the table including Summary). The table is identified by
# the header line `| Rule | Total firings | ...`.
#
# Note: the baseline script emits a blank line between the `|------|` row
# and the first data row, then optional blank lines after the last data
# row before the footer. We treat blank lines INSIDE the table as
# separators (skip + keep buffering), and the table ends at the first
# `## ` heading or any non-blank, non-pipe line.
$mode = 'pre'
$trailingBlanks = New-Object System.Collections.ArrayList
foreach ($ln in $lines) {
    if ($mode -eq 'pre') {
        [void]$preamble.Add($ln)
        if ($ln -match '^\|------\|') {
            $mode = 'tab'
        }
        continue
    }
    if ($mode -eq 'tab') {
        if ($ln.Trim() -eq '') {
            # Buffer the blank — we don't know yet if the table is over.
            [void]$trailingBlanks.Add($ln)
            continue
        }
        if ($ln -match '^\|') {
            # Buffered blanks belong inside the table (preserve order).
            foreach ($b in $trailingBlanks) { [void]$tableRows.Add($b) }
            $trailingBlanks.Clear()
            [void]$tableRows.Add($ln)
            continue
        }
        # Anything else (heading / prose) ends the table.
        $mode = 'post'
        foreach ($b in $trailingBlanks) { [void]$footer.Add($b) }
        $trailingBlanks.Clear()
        [void]$footer.Add($ln)
        continue
    }
    [void]$footer.Add($ln)
}
# Any blanks still buffered at EOF go to the footer.
foreach ($b in $trailingBlanks) { [void]$footer.Add($b) }

# The Above-budget block strip happened before parsing into the
# preamble/table/footer split, so $preamble is already clean. We keep
# the same variable name so the splice logic below is unchanged.
$preambleClean = $preamble

# --- Read corpus firing detail directly from the CLI ---------------------
#
# Triage decisions need the per-shader firing list (which categories fired
# on which corpus shaders). The base markdown table only carries totals;
# we reconstruct the detail by re-running `shader-clippy lint --format=json`
# across the corpus, mirroring fp-rate-baseline.ps1.

$CliPath   = Join-Path $RepoRoot 'build\cli\shader-clippy.exe'
$CorpusDir = Join-Path $RepoRoot 'tests\corpus'
if (-not (Test-Path -LiteralPath $CliPath)) {
    Write-Error "fp-rate-triage: cli not found at $CliPath. Run cmake --build build first."
    exit 1
}

$shaders = Get-ChildItem -LiteralPath $CorpusDir -Recurse -Filter '*.hlsl' |
    Where-Object { -not $_.PSIsContainer } |
    Sort-Object FullName

# rule-id -> [{ shader (relative path), line }]
$findings = @{}

Write-Host "fp-rate-triage: re-scanning $($shaders.Count) shader(s) for triage detail"
foreach ($sh in $shaders) {
    $rel = $sh.FullName.Substring($RepoRoot.Length + 1).Replace('\', '/')
    # Strip the tests/corpus/ prefix so the prefix matches in the decision tables.
    $relCorpus = $rel
    if ($relCorpus.StartsWith('tests/corpus/')) {
        $relCorpus = $relCorpus.Substring('tests/corpus/'.Length)
    }

    $json = & $CliPath lint --format=json $sh.FullName 2>$null
    if ([string]::IsNullOrWhiteSpace($json)) { continue }
    try { $diags = $json | ConvertFrom-Json } catch { continue }
    if ($diags -isnot [array]) { $diags = @($diags) }

    foreach ($d in $diags) {
        if (-not $d.PSObject.Properties['rule']) { continue }
        $rule = $d.rule
        if (-not $findings.ContainsKey($rule)) {
            $findings[$rule] = New-Object System.Collections.ArrayList
        }
        [void]$findings[$rule].Add(@{
            shader = $relCorpus
            line   = $d.line
        })
    }
}

# --- Apply triage decision per rule --------------------------------------

function Get-TriageDecision {
    param(
        [string]$Rule,
        $Firings  # ArrayList of { shader; line } hashtables
    )

    # Skip clippy::* infrastructure diagnostics.
    if ($Rule -like 'clippy::*') {
        return @{ Verdict = 'NEEDS-HUMAN'; TP = 0; FP = 0; Note = 'infrastructure diagnostic' }
    }

    if ($RuleHeuristicOverfire.ContainsKey($Rule)) {
        return @{
            Verdict = 'FP'
            TP = 0
            FP = $Firings.Count
            Note = $RuleHeuristicOverfire[$Rule]
        }
    }

    if (-not $RuleNaturalDomains.ContainsKey($Rule)) {
        return @{ Verdict = 'NEEDS-HUMAN'; TP = 0; FP = 0; Note = 'no domain mapping' }
    }

    $domains = $RuleNaturalDomains[$Rule]
    $tp = 0
    $fp = 0
    foreach ($f in $Firings) {
        $matched = $false
        foreach ($pfx in $domains) {
            if ($f.shader.StartsWith($pfx)) { $matched = $true; break }
        }
        if ($matched) { $tp++ } else { $fp++ }
    }

    if ($tp -gt 0 -and $fp -eq 0) {
        return @{ Verdict = 'TP'; TP = $tp; FP = 0; Note = '' }
    }
    if ($tp -eq 0 -and $fp -gt 0) {
        return @{ Verdict = 'FP'; TP = 0; FP = $fp; Note = 'all firings outside natural domain' }
    }
    return @{ Verdict = 'MIXED'; TP = $tp; FP = $fp; Note = '' }
}

# --- Walk the existing rows + apply triage to TODO rows ------------------

$updatedRows = New-Object System.Collections.ArrayList
$aboveBudget = New-Object System.Collections.ArrayList

# Header rows are first two lines of $tableRows (`| Rule | ...|` and the
# `|------|`); they were placed in $preamble in our parse. So $tableRows
# starts at the first data row. Defensive: skip any row whose first cell
# is `Rule`.
foreach ($row in $tableRows) {
    $cells = $row -split '\|'
    # Splitting `| a | b |` yields ['', ' a ', ' b ', ''].
    if ($cells.Count -lt 6) {
        [void]$updatedRows.Add($row)
        continue
    }
    $ruleCell    = $cells[1].Trim()
    $totalCell   = $cells[2].Trim()
    $shadersCell = $cells[3].Trim()
    $triageCell  = $cells[4].Trim()
    # cells[5] is the existing FP-rate column; we recompute it below.

    # Strip backticks the baseline script emits around the rule id.
    $ruleId = $ruleCell.Trim('`').Trim()

    # Preserve manually-edited rows; only update entries still TODO.
    $isTodo = ($triageCell -eq 'TODO') -or ($triageCell -eq '')
    if (-not $isTodo) {
        [void]$updatedRows.Add($row)
        # Even for preserved rows, feed the above-budget summary if the
        # cell is parseable. We accept any of:
        #   "<n> TP / <m> FP"
        #   "TP (<n> TP / <m> FP)"
        #   "FP (<n> TP / <m> FP) -- ..."
        #   "MIXED -- <n> TP / <m> FP"
        if ($triageCell -match '(\d+)\s*TP\s*/\s*(\d+)\s*FP') {
            $tp = [int]$Matches[1]; $fp = [int]$Matches[2]
            if (($tp + $fp) -gt 0) {
                $fpRate = $fp / ($tp + $fp)
                if ($fpRate -gt 0.05) {
                    $note = 'preserved from previous triage'
                    if ($RuleHeuristicOverfire.ContainsKey($ruleId)) {
                        $note = $RuleHeuristicOverfire[$ruleId]
                    }
                    [void]$aboveBudget.Add(@{ Rule=$ruleId; Rate=$fpRate; TP=$tp; FP=$fp; Note=$note })
                }
            }
        }
        continue
    }

    # Compute fresh decision.
    $firings = if ($findings.ContainsKey($ruleId)) { $findings[$ruleId] } else { @() }
    $decision = Get-TriageDecision -Rule $ruleId -Firings $firings

    $verdict = $decision.Verdict
    $tp = $decision.TP
    $fp = $decision.FP
    $note = $decision.Note

    switch ($verdict) {
        'TP'    { $newTriage = "TP ($tp TP / 0 FP)" ; $newRate = '0%' }
        'FP'    {
            $newTriage = "FP ($tp TP / $fp FP)"
            if ($note) { $newTriage = "$newTriage -- $note" }
            $newRate = '100%'
        }
        'MIXED' {
            $newTriage = "MIXED -- $tp TP / $fp FP"
            if (($tp + $fp) -gt 0) {
                $rate = $fp / ($tp + $fp)
                $newRate = '{0:N1}%' -f ($rate * 100)
            } else {
                $newRate = 'n/a'
            }
        }
        default {
            $newTriage = "NEEDS-HUMAN"
            if ($note) { $newTriage = "NEEDS-HUMAN -- $note" }
            $newRate   = 'TBD'
        }
    }

    if (($tp + $fp) -gt 0) {
        $rate = $fp / ($tp + $fp)
        if ($rate -gt 0.05) {
            [void]$aboveBudget.Add(@{ Rule=$ruleId; Rate=$rate; TP=$tp; FP=$fp; Note=$note })
        }
    }

    $newRow = "| ``$ruleId`` | $totalCell | $shadersCell | $newTriage | $newRate |"
    [void]$updatedRows.Add($newRow)
}

# --- Render the "Above-budget rules" preamble section --------------------

$aboveBudgetSection = New-Object System.Collections.ArrayList
[void]$aboveBudgetSection.Add('## Above-budget rules (FP rate > 5%)')
[void]$aboveBudgetSection.Add('')
[void]$aboveBudgetSection.Add('Per ADR 0018 §5 criterion #3, every warn-severity rule must carry an FP rate <= 5% against the corpus. The rules below exceed that budget on the v1.1 deterministic triage pass and need maintainer review (typically: tighten the rule''s detection, suppress the rule on the offending category via `.shader-clippy.toml`, or downgrade severity).')
[void]$aboveBudgetSection.Add('')
if ($aboveBudget.Count -eq 0) {
    [void]$aboveBudgetSection.Add('_(none — every rule is within the 5% FP-rate budget.)_')
} else {
    [void]$aboveBudgetSection.Add('| Rule | TP | FP | FP rate | Note |')
    [void]$aboveBudgetSection.Add('|------|----|----|---------|------|')
    $aboveBudgetSorted = $aboveBudget | Sort-Object -Property @{ Expression={ $_.Rate }; Descending=$true }
    foreach ($r in $aboveBudgetSorted) {
        $rateStr = '{0:N1}%' -f ($r.Rate * 100)
        $note = if ($r.Note) { $r.Note } else { '' }
        [void]$aboveBudgetSection.Add("| ``$($r.Rule)`` | $($r.TP) | $($r.FP) | $rateStr | $note |")
    }
}
[void]$aboveBudgetSection.Add('')

# --- Splice the new section in BEFORE the table block --------------------
#
# We insert the "Above-budget rules" section immediately before the line
# `| Rule | Total firings | ... |` so the reader sees the at-a-glance
# offenders before the per-rule firing count table.

$finalLines = New-Object System.Collections.ArrayList
$inserted = $false
foreach ($ln in $preambleClean) {
    if (-not $inserted -and $ln -match '^\| Rule \|') {
        foreach ($s in $aboveBudgetSection) { [void]$finalLines.Add($s) }
        $inserted = $true
    }
    [void]$finalLines.Add($ln)
}
foreach ($r in $updatedRows) { [void]$finalLines.Add($r) }
foreach ($f in $footer) { [void]$finalLines.Add($f) }

$content = ($finalLines -join "`n")

if ($DryRun) {
    Write-Host "fp-rate-triage: -DryRun set; not writing $OutputMd"
    Write-Host "triaged rows: $($updatedRows.Count)"
    foreach ($r in $updatedRows) { Write-Host "  $r" }
    Write-Host "above-budget rules: $($aboveBudget.Count)"
    foreach ($r in $aboveBudget) {
        Write-Host ("  - {0} (TP={1}, FP={2}, rate={3:N1}%)" -f $r.Rule, $r.TP, $r.FP, ($r.Rate * 100))
    }
    return
}

Set-Content -LiteralPath $OutputMd -Value $content -Encoding UTF8

Write-Host "fp-rate-triage: wrote $OutputMd"
Write-Host "  triaged rows : $($updatedRows.Count)"
Write-Host "  above-budget : $($aboveBudget.Count)"
foreach ($r in $aboveBudget) {
    Write-Host ("    - {0} (TP={1}, FP={2}, rate={3:N1}%)" -f $r.Rule, $r.TP, $r.FP, ($r.Rate * 100))
}
