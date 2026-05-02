# tools/fix-blog-stub-titles.ps1
#
# Fixes the YAML front-matter `title:` field on the 204 v1.0.0 stub
# blog posts. The original generator embedded the rule's first-paragraph
# summary into the title, which contained backticks, double-quotes,
# parentheses, and the U+2026 ellipsis -- all of which YAML's
# parser refuses to handle inside a double-quoted scalar.
#
# Replaces:
#   title: "<rule-id>: <prose with embedded "quotes" and `backticks` ...>"
# With:
#   title: "<rule-id>"
#
# Idempotent: stub posts that already have the clean form are no-ops.
# Skips files whose `status:` is NOT `stub` (existing full posts and
# launch-overview posts stay untouched).

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
$BlogDir   = Join-Path $RepoRoot 'docs\blog'

$updated = 0
$skipped = 0

Get-ChildItem -LiteralPath $BlogDir -Filter '*.md' -File | ForEach-Object {
    $path = $_.FullName
    $content = Get-Content -LiteralPath $path -Raw

    # Only touch stubs.
    if ($content -notmatch '(?m)^status:\s*stub\s*$') {
        $skipped++
        return
    }

    # Match `title: "<rule-id>: <anything>"` and replace with just the rule-id.
    # Use [\s\S] to allow embedded newlines (defensive; titles are usually
    # one-line but YAML allows multi-line scalars).
    $newContent = $content -replace `
        '(?m)^title:\s*"([a-z0-9-]+):\s*[\s\S]+?"\s*$', `
        'title: "$1"'

    if ($newContent -ne $content) {
        Set-Content -LiteralPath $path -Value $newContent -NoNewline -Encoding UTF8
        $updated++
    }
}

Write-Host "fix-blog-stub-titles: updated=$updated, skipped=$skipped (non-stub)"
