#requires -Version 5.1
# One-shot sweeper: factor `node_kind`, `node_text`, and `is_id_char` out of
# every rule TU under `core/src/rules/` into the shared
# `core/src/rules/util/ast_helpers.{hpp,cpp}` introduced in this commit.
#
# For each .cpp:
#   1. Detect which of the three helpers are defined locally (anon ns).
#   2. Drop the local definitions.
#   3. Add `#include "rules/util/ast_helpers.hpp"` after `#include "rules.hpp"`.
#   4. Inject `using util::<helper>;` lines just inside the anonymous ns.
#
# Bails (does not rewrite) on files where the helper signature line cannot
# be matched against a `}\n` close at column 0.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$rulesDir = Join-Path $root 'core\src\rules'

$signatures = @{
    'node_kind'  = '[[nodiscard]] std::string_view node_kind(::TSNode'
    'node_text'  = '[[nodiscard]] std::string_view node_text(::TSNode'
    'is_id_char' = '[[nodiscard]] bool is_id_char(char'
}

$rewritten = 0
$failed = @()

foreach ($cpp in Get-ChildItem -Path $rulesDir -Filter *.cpp -File) {
    $text = [System.IO.File]::ReadAllText($cpp.FullName)
    $lines = $text -split "`r?`n"

    $defined = @()
    $rangesToRemove = New-Object System.Collections.Generic.List[pscustomobject]
    $bail = $false

    foreach ($helper in $signatures.Keys) {
        $sig = $signatures[$helper]
        $startIdx = -1
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i].StartsWith($sig)) {
                $startIdx = $i
                break
            }
        }
        if ($startIdx -lt 0) { continue }

        $endIdx = -1
        for ($j = $startIdx + 1; $j -lt $lines.Count; $j++) {
            if ($lines[$j] -ceq '}') {
                $endIdx = $j
                break
            }
        }
        if ($endIdx -lt 0) {
            $failed += [pscustomobject]@{ File = $cpp.Name; Reason = "no closing brace for $helper" }
            $bail = $true
            break
        }

        $defined += $helper
        $rangesToRemove.Add([pscustomobject]@{ Start = $startIdx; End = $endIdx }) | Out-Null
    }

    if ($bail) { continue }
    if ($defined.Count -eq 0) { continue }

    $newLines = New-Object System.Collections.Generic.List[string]
    $newLines.AddRange([string[]]$lines)

    # Walk removals in descending start order so earlier ranges remain valid.
    $sortedRanges = $rangesToRemove | Sort-Object -Property Start -Descending

    foreach ($range in $sortedRanges) {
        $start = [int]$range.Start
        $end = [int]$range.End
        # Also remove the trailing blank line, if any.
        if (($end + 1) -lt $newLines.Count -and $newLines[$end + 1] -eq '') {
            $end = $end + 1
        }
        $count = $end - $start + 1
        if ($count -le 0) { continue }
        $newLines.RemoveRange($start, $count)
    }

    # Add include.
    $haveInclude = $false
    foreach ($line in $newLines) {
        if ($line -eq '#include "rules/util/ast_helpers.hpp"') { $haveInclude = $true; break }
    }
    if (-not $haveInclude) {
        $anchorIdx = -1
        # Prefer `rules.hpp` anchor; fall back to `parser_internal.hpp` for
        # rule TUs that do not use the rule-registry header (a few SM 6.9 /
        # math rules include only the public surface).
        foreach ($anchor in @('#include "rules.hpp"', '#include "parser_internal.hpp"')) {
            for ($i = 0; $i -lt $newLines.Count; $i++) {
                if ($newLines[$i] -eq $anchor) { $anchorIdx = $i; break }
            }
            if ($anchorIdx -ge 0) { break }
        }
        if ($anchorIdx -lt 0) {
            $failed += [pscustomobject]@{ File = $cpp.Name; Reason = 'no rules.hpp / parser_internal.hpp include anchor' }
            continue
        }
        $newLines.Insert($anchorIdx + 1, '#include "rules/util/ast_helpers.hpp"')
    }

    # Add `using` declarations after the anonymous namespace opener.
    # Look for `namespace {` standing alone after `namespace hlsl_clippy::rules
    # {` (possibly separated by a blank line).
    $nsIdx = -1
    for ($i = 0; $i -lt $newLines.Count; $i++) {
        if ($newLines[$i] -eq 'namespace {') {
            # Confirm the previous non-blank line is the parent namespace.
            $back = $i - 1
            while ($back -ge 0 -and $newLines[$back] -eq '') { $back-- }
            if ($back -ge 0 -and $newLines[$back] -eq 'namespace hlsl_clippy::rules {') {
                $nsIdx = $i
                break
            }
        }
    }
    if ($nsIdx -lt 0) {
        $failed += [pscustomobject]@{ File = $cpp.Name; Reason = 'no anonymous namespace anchor' }
        continue
    }

    # Insert using declarations after the `namespace {` line. Order matches
    # the canonical helper enumeration; files that already declare a using
    # for the same name are skipped so the sweeper is idempotent.
    $usingLines = @()
    foreach ($helper in @('node_kind', 'node_text', 'is_id_char')) {
        if ($defined -contains $helper) {
            $usingLine = "using util::$helper;"
            $alreadyHave = $false
            foreach ($line in $newLines) { if ($line -eq $usingLine) { $alreadyHave = $true; break } }
            if (-not $alreadyHave) { $usingLines += $usingLine }
        }
    }
    if ($usingLines.Count -gt 0) {
        # Inject after the `namespace {` line, with one blank above and below.
        $insertion = @('') + $usingLines
        for ($k = $insertion.Count - 1; $k -ge 0; $k--) {
            $newLines.Insert($nsIdx + 1, $insertion[$k])
        }
    }

    $newText = [string]::Join("`n", $newLines)
    # Collapse any 3+ blank-line runs created by removal.
    $newText = [regex]::Replace($newText, "`n{3,}", "`n`n")

    [System.IO.File]::WriteAllText($cpp.FullName, $newText)
    $rewritten++
}

Write-Output "rewritten: $rewritten"
if ($failed.Count -gt 0) {
    Write-Output "failed: $($failed.Count)"
    foreach ($f in $failed) {
        Write-Output ("  {0}: {1}" -f $f.File, $f.Reason)
    }
    exit 1
}
