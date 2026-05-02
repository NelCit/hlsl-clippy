# Sweep docs/rules/*.md and replace the stale
#   "Companion blog post: _not yet published[...]_"
# bullet with a link to the per-rule post (pow-const-squared) or the
# per-category overview post for everything else.
#
# Mapping is based on the rule's `category:` frontmatter -> blog post under
# docs/blog/. Rules whose category has no overview (only `misc` today) are
# left alone with the original placeholder.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$docs = Join-Path $root 'docs\rules'

$categoryToPost = @{
    'math'                = 'math-overview'
    'packed-math'         = 'math-overview'
    'saturate-redundancy' = 'math-overview'
    'bindings'            = 'bindings-overview'
    'control-flow'        = 'control-flow-overview'
    'workgroup'           = 'workgroup-overview'
    'memory'              = 'workgroup-overview'
    'texture'             = 'texture-overview'
    'sampler-feedback'    = 'texture-overview'
    'vrs'                 = 'texture-overview'
    'ser'                 = 'ser-coop-vector-overview'
    'cooperative-vector'  = 'ser-coop-vector-overview'
    'long-vectors'        = 'ser-coop-vector-overview'
    'opacity-micromaps'   = 'ser-coop-vector-overview'
    'mesh'                = 'mesh-dxr-overview'
    'dxr'                 = 'mesh-dxr-overview'
    'work-graphs'         = 'mesh-dxr-overview'
    'wave-helper-lane'    = 'wave-helper-lane-overview'
}

$rewritten = 0
$skippedMisc = 0
$skippedNoMatch = 0

# Match the placeholder in any of its known phrasings:
#   - "_not yet published_"
#   - "_not yet published [-- ]will appear alongside the v0.1 release_"
$placeholderRe = '(?m)^- Companion blog post: _not yet published[^_]*_'

foreach ($md in Get-ChildItem -Path $docs -Filter '*.md' -File) {
    if ($md.Name -eq 'index.md' -or $md.Name -eq '_template.md') { continue }
    $text = [System.IO.File]::ReadAllText($md.FullName)
    if ($text -notmatch $placeholderRe) { continue }
    if ($text -notmatch '(?m)^category:\s*(\S+)') { continue }
    $cat = $matches[1].Trim('"')
    $base = $md.BaseName

    # Rule with a dedicated post takes priority over the category overview.
    if ($base -eq 'pow-const-squared') {
        $linkText = '[Where the cycles go: pow(x, 2.0)](../blog/pow-const-squared.md)'
    } elseif ($categoryToPost.ContainsKey($cat)) {
        $post = $categoryToPost[$cat]
        $linkText = "[$cat overview](../blog/$post.md)"
    } elseif ($cat -eq 'misc') {
        $skippedMisc++
        continue
    } else {
        $skippedNoMatch++
        continue
    }

    $replacement = '- Companion blog post: ' + $linkText
    $newText = [regex]::Replace($text, $placeholderRe, $replacement)
    [System.IO.File]::WriteAllText($md.FullName, $newText)
    $rewritten++
}

Write-Output "rewritten: $rewritten"
Write-Output "skipped (misc category, no overview): $skippedMisc"
Write-Output "skipped (unknown category, no overview): $skippedNoMatch"
