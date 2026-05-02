#!/usr/bin/env bash
# tools/adoption-poll.sh
#
# Linux / macOS port of tools/adoption-poll.ps1. See that file's
# header comment for the full rationale + dependency list.
#
# Polls Marketplace install count + GitHub-code-search hit count for
# downstream-integration evidence. Appends one dated row to
# `docs/adoption-metrics.md`. Threshold validation is a human task —
# this script only captures the data.
#
# Usage (from repo root):
#   bash tools/adoption-poll.sh
#   bash tools/adoption-poll.sh --dry-run

set -euo pipefail

DRY_RUN=0
for arg in "$@"; do
  case "$arg" in
    --dry-run|-n) DRY_RUN=1 ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
    *)
      echo "adoption-poll: unknown flag: $arg" >&2
      exit 1
      ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_MD="$REPO_ROOT/docs/adoption-metrics.md"

# --- 1. Marketplace listing ----------------------------------------------

installs='?'
rating='?'
version='?'
mp_updated='?'

if ! command -v vsce >/dev/null 2>&1; then
  echo "adoption-poll: 'vsce' not on PATH. Install with 'npm i -g @vscode/vsce'." >&2
else
  vsce_raw=$(vsce show 'nelcit.hlsl-clippy' --json 2>/dev/null || true)
  if [ -z "$vsce_raw" ]; then
    echo "adoption-poll: 'vsce show' produced no output." >&2
  elif ! command -v jq >/dev/null 2>&1; then
    echo "adoption-poll: 'jq' not on PATH; cannot parse vsce JSON." >&2
  else
    # Parse the same fields the PS1 variant captures. `jq` returns
    # `null` literals for missing keys; collapse them to '?'.
    installs=$(  echo "$vsce_raw" | jq -r '(.statistics // [] | map(select(.statisticName == "install"))       | .[0].value       // "?")')
    rating=$(    echo "$vsce_raw" | jq -r '(.statistics // [] | map(select(.statisticName == "averagerating")) | .[0].value       // "?")')
    version=$(   echo "$vsce_raw" | jq -r '(.versions   // [] | .[0].version                                                       // "?")')
    mp_updated=$(echo "$vsce_raw" | jq -r '(.versions   // [] | .[0].lastUpdated                                                   // "?")')
  fi
fi

# --- 2. Downstream integrations -------------------------------------------

downstream_count='?'
top_repos=''

if ! command -v gh >/dev/null 2>&1; then
  echo "adoption-poll: 'gh' not on PATH. Install GitHub CLI + 'gh auth login'." >&2
else
  if ! command -v jq >/dev/null 2>&1; then
    echo "adoption-poll: 'jq' not on PATH; cannot parse gh JSON." >&2
  else
    gh_raw=$(gh search code 'hlsl-clippy filename:.github/workflows' \
      --json repository --limit 100 2>/dev/null || true)
    if [ -n "$gh_raw" ]; then
      # Dedup repos client-side, drop the project's own repo, count.
      mapfile -t repos < <(echo "$gh_raw" \
        | jq -r '.[] | (.repository.nameWithOwner // .repository.fullName // empty)' \
        | grep -v -i '^NelCit/hlsl-clippy$' \
        | sort -u)
      downstream_count="${#repos[@]}"
      if [ "${#repos[@]}" -gt 0 ]; then
        # Top-5; trail with "+ N more" if there are extras.
        n="${#repos[@]}"
        if [ "$n" -gt 5 ]; then
          top_repos="$(IFS=', '; echo "${repos[*]:0:5}"), + $((n - 5)) more"
        else
          top_repos="$(IFS=', '; echo "${repos[*]}")"
        fi
      fi
    else
      echo "adoption-poll: 'gh search code' produced no output." >&2
    fi
  fi
fi

# --- 3. Render row + append (or create) ---------------------------------

today=$(date -u '+%Y-%m-%d')
row="| $today | $installs | $rating | $version | $mp_updated | $downstream_count | $top_repos |"

echo "adoption-poll: $row"

if [ "$DRY_RUN" = "1" ]; then
  echo "adoption-poll: --dry-run set; not appending to $OUTPUT_MD"
  exit 0
fi

if [ ! -f "$OUTPUT_MD" ]; then
  cat > "$OUTPUT_MD" <<'EOF'
# Adoption metrics — `nelcit.hlsl-clippy`

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
EOF
fi

printf '%s\n' "$row" >> "$OUTPUT_MD"
echo "adoption-poll: appended row to $OUTPUT_MD"
