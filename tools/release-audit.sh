#!/usr/bin/env bash
#
# tools/release-audit.sh
#
# Pre-tag release-readiness audit (ADR 0018 §5 criterion #12).
#
# Walks six checks against the working tree + git history and reports
# pass/fail per check:
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
#   6. Public hdrs  — every file under `core/include/shader_clippy/` has
#                     `#pragma once` and ends in `.hpp`.
#
# Usage:
#   bash tools/release-audit.sh 1.0.0
#
# Exits 0 when all checks pass, 1 otherwise. Per-check pass/fail is
# printed even when later checks short-circuit.
#
# POSIX-compatible enough to run on Ubuntu 24.04 + macOS 14 (Apple
# Silicon). No PS7-only / GNU-specific extensions; we use BRE with sed
# and the busybox-friendly `head -n` form.

set -eu
# Note: `set -o pipefail` is intentionally NOT set — we want a failed
# `grep` exit code in a pipeline to surface as a per-check failure, not
# as a script-wide bail-out. Each check tracks its own pass/fail bit.

# --- Arg parsing -----------------------------------------------------------
TAG_VERSION=""
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            sed -n '3,32p' "$0"
            exit 0
            ;;
        -*)
            echo "release-audit: unknown option '$1'" >&2
            echo "release-audit: usage: bash tools/release-audit.sh <tag-version>" >&2
            exit 2
            ;;
        *)
            if [ -n "$TAG_VERSION" ]; then
                echo "release-audit: too many positional arguments" >&2
                exit 2
            fi
            TAG_VERSION="$1"
            shift
            ;;
    esac
done

if [ -z "$TAG_VERSION" ]; then
    echo "release-audit: missing tag version (e.g. 1.0.0)" >&2
    echo "release-audit: usage: bash tools/release-audit.sh <tag-version>" >&2
    exit 2
fi

# --- Locate repo root ------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Per-check results. We track pass/fail per check and aggregate at the end.
PASS_COUNT=0
FAIL_COUNT=0

pass() {
    printf '  PASS — %s\n' "$1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    printf '  FAIL — %s\n' "$1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

# --- Resolve previous tag ---------------------------------------------------
# `git describe --abbrev=0 --tags HEAD^` would resolve the last tag before
# HEAD. We tolerate the no-tags case (fresh repo) by falling back to the
# root commit.
PREV_TAG="$(git -C "$REPO_ROOT" describe --abbrev=0 --tags 2>/dev/null || true)"
if [ -z "$PREV_TAG" ]; then
    PREV_REF="$(git -C "$REPO_ROOT" rev-list --max-parents=0 HEAD | head -n 1)"
    PREV_LABEL="<root>"
else
    PREV_REF="$PREV_TAG"
    PREV_LABEL="$PREV_TAG"
fi

echo "release-audit: tag-to-be     = v${TAG_VERSION}"
echo "release-audit: previous tag  = ${PREV_LABEL}"
echo "release-audit: repo root     = ${REPO_ROOT}"
echo

# ──────────────────────────────────────────────────────────────────────────
# Check 1 — DCO sign-off on every commit since the previous tag.
# ──────────────────────────────────────────────────────────────────────────
echo "[1/6] DCO sign-off (every commit since ${PREV_LABEL})"
DCO_FAILS=0
# `--format=%H%x1f%ae%x1f%B%x1e` separates fields by 0x1f and commits by
# 0x1e so we can split on shell-safe delimiters. busybox awk supports
# split-by-string with -F so this works on macOS BSD awk too.
git -C "$REPO_ROOT" log "${PREV_REF}..HEAD" --format=$'%H\t%ae\t%B\x1e' \
    | awk 'BEGIN { RS="\x1e" } NF > 0 { print }' \
    | while IFS= read -r record; do
    [ -z "$record" ] && continue
    sha=$(printf '%s' "$record" | head -n 1 | cut -f1)
    email=$(printf '%s' "$record" | head -n 1 | cut -f2)
    body=$(printf '%s' "$record" | tail -n +1)
    # Detect at least one `Signed-off-by:` trailer whose email matches
    # the author. We accept any RFC-5322-ish email match — strict
    # comparison would reject legit GitHub no-reply sigs.
    if printf '%s' "$body" \
            | grep -E -q "^Signed-off-by:[[:space:]].*<${email}>" \
        ; then
        :
    else
        printf '    missing/mismatched sign-off: %s by %s\n' \
            "$(printf '%s' "$sha" | cut -c1-12)" "$email" >&2
        # We can't increment a parent-shell variable from a `while` in a
        # pipeline (subshell). Encode failure via a touched marker.
        printf '\n' >> "$REPO_ROOT/.release-audit-dco-fail"
    fi
done
if [ -f "$REPO_ROOT/.release-audit-dco-fail" ]; then
    DCO_FAILS=$(wc -l < "$REPO_ROOT/.release-audit-dco-fail" | tr -d ' ')
    rm -f "$REPO_ROOT/.release-audit-dco-fail"
fi
if [ "$DCO_FAILS" -eq 0 ]; then
    pass "every commit since ${PREV_LABEL} carries a matching Signed-off-by"
else
    fail "${DCO_FAILS} commit(s) since ${PREV_LABEL} are missing a matching Signed-off-by"
fi

# ──────────────────────────────────────────────────────────────────────────
# Check 2 — Conventional Commits on every commit since the previous tag.
# ──────────────────────────────────────────────────────────────────────────
echo "[2/6] Conventional Commits subject lines"
CC_PATTERN='^(feat|fix|refactor|docs|chore|test|build|ci|release)(\([^)]+\))?!?:[[:space:]].+'
CC_FAILS=0
# Subject line = first line of the commit message.
git -C "$REPO_ROOT" log "${PREV_REF}..HEAD" --format='%H %s' \
    | while IFS= read -r line; do
    [ -z "$line" ] && continue
    sha=$(printf '%s' "$line" | cut -d ' ' -f 1)
    subject=$(printf '%s' "$line" | cut -d ' ' -f 2-)
    if ! printf '%s' "$subject" | grep -E -q "$CC_PATTERN"; then
        printf '    non-conformant subject: %s — "%s"\n' \
            "$(printf '%s' "$sha" | cut -c1-12)" "$subject" >&2
        printf '\n' >> "$REPO_ROOT/.release-audit-cc-fail"
    fi
done
if [ -f "$REPO_ROOT/.release-audit-cc-fail" ]; then
    CC_FAILS=$(wc -l < "$REPO_ROOT/.release-audit-cc-fail" | tr -d ' ')
    rm -f "$REPO_ROOT/.release-audit-cc-fail"
fi
if [ "$CC_FAILS" -eq 0 ]; then
    pass "every commit subject since ${PREV_LABEL} matches Conventional Commits"
else
    fail "${CC_FAILS} commit subject(s) since ${PREV_LABEL} do not match Conventional Commits"
fi

# ──────────────────────────────────────────────────────────────────────────
# Check 3 — CHANGELOG.md has a `## [<version>]` heading for the new tag.
# ──────────────────────────────────────────────────────────────────────────
echo "[3/6] CHANGELOG entry for ${TAG_VERSION}"
CHANGELOG="$REPO_ROOT/CHANGELOG.md"
if [ ! -f "$CHANGELOG" ]; then
    fail "CHANGELOG.md is missing at repo root"
elif grep -E -q "^## \[${TAG_VERSION}\]" "$CHANGELOG"; then
    pass "CHANGELOG.md has '## [${TAG_VERSION}]' heading"
else
    fail "CHANGELOG.md has no '## [${TAG_VERSION}]' heading"
fi

# ──────────────────────────────────────────────────────────────────────────
# Check 4 — version strings match across all canonical sources + tag.
# ──────────────────────────────────────────────────────────────────────────
echo "[4/6] Version strings cross-check"
CORE_VERSION_FILE="$REPO_ROOT/core/src/version.cpp"
VSC_PKG_FILE="$REPO_ROOT/vscode-extension/package.json"

# Pull each canonical source. Use grep -oE with a tight regex so we don't
# accidentally pick up unrelated quoted values.
CORE_VER=""
if [ -f "$CORE_VERSION_FILE" ]; then
    CORE_VER=$(grep -oE 'return "[^"]+"' "$CORE_VERSION_FILE" \
        | head -n 1 \
        | sed -E 's/.*"([^"]+)".*/\1/' || true)
fi

VSC_VER=""
if [ -f "$VSC_PKG_FILE" ]; then
    # `"version": "X.Y.Z"` — the first match in package.json is the
    # extension version (publisher / name come earlier alphabetically
    # but use different keys).
    VSC_VER=$(grep -oE '"version"[[:space:]]*:[[:space:]]*"[^"]+"' "$VSC_PKG_FILE" \
        | head -n 1 \
        | sed -E 's/.*"([^"]+)"$/\1/' || true)
fi

CHANGELOG_VER=""
if [ -f "$CHANGELOG" ]; then
    CHANGELOG_VER=$(grep -oE '^## \[[0-9][^]]*\]' "$CHANGELOG" \
        | head -n 1 \
        | sed -E 's/^## \[(.+)\]$/\1/' || true)
fi

VERSION_MISMATCH=0
report_version() {
    local label="$1"
    local actual="$2"
    if [ "$actual" = "$TAG_VERSION" ]; then
        printf '    %-32s = %s (match)\n' "$label" "$actual"
    else
        printf '    %-32s = %s (expected %s)\n' \
            "$label" "${actual:-<missing>}" "$TAG_VERSION" >&2
        VERSION_MISMATCH=$((VERSION_MISMATCH + 1))
    fi
}
report_version "core/src/version.cpp"            "$CORE_VER"
report_version "vscode-extension/package.json"   "$VSC_VER"
report_version "CHANGELOG.md latest heading"     "$CHANGELOG_VER"
if [ "$VERSION_MISMATCH" -eq 0 ]; then
    pass "all four version sources agree on ${TAG_VERSION}"
else
    fail "${VERSION_MISMATCH} version source(s) do not match ${TAG_VERSION}"
fi

# ──────────────────────────────────────────────────────────────────────────
# Check 5 — every Accepted ADR file is in the CLAUDE.md table as Accepted.
# ──────────────────────────────────────────────────────────────────────────
echo "[5/6] ADR index sync (CLAUDE.md ↔ docs/decisions/)"
DECISIONS_DIR="$REPO_ROOT/docs/decisions"
CLAUDE_MD="$REPO_ROOT/CLAUDE.md"
ADR_FAILS=0
if [ ! -d "$DECISIONS_DIR" ] || [ ! -f "$CLAUDE_MD" ]; then
    fail "missing $DECISIONS_DIR or $CLAUDE_MD"
else
    for adr in "$DECISIONS_DIR"/[0-9][0-9][0-9][0-9]-*.md; do
        [ -e "$adr" ] || continue
        # Read the YAML front-matter `status:` line. We only enforce the
        # Accepted-must-be-listed direction (Proposed / Rejected / Deprecated
        # need not appear).
        status=$(awk '
            /^---$/ { hdr_seen++; if (hdr_seen == 2) exit; next }
            hdr_seen == 1 && /^status:/ { sub(/^status:[[:space:]]*/, ""); print; exit }
        ' "$adr" | tr -d '\r')
        # Strip surrounding quotes if any.
        status=$(printf '%s' "$status" | sed -E 's/^"(.*)"$/\1/' | sed -E "s/^'(.*)'$/\1/")
        if [ "$status" != "Accepted" ]; then
            continue
        fi
        adr_basename=$(basename "$adr")
        # Look for a row in the CLAUDE.md ADR-index table that mentions
        # the basename AND is tagged `Accepted`. The table currently uses
        # `[NNNN](docs/decisions/<basename>) | Title | Accepted` so we
        # match on the basename anywhere on the same line as `Accepted`.
        if grep -F "$adr_basename" "$CLAUDE_MD" \
                | grep -F -q "Accepted"; then
            :
        else
            printf '    %s is Accepted but not listed Accepted in CLAUDE.md\n' \
                "$adr_basename" >&2
            ADR_FAILS=$((ADR_FAILS + 1))
        fi
    done
    if [ "$ADR_FAILS" -eq 0 ]; then
        pass "every Accepted ADR appears as Accepted in CLAUDE.md"
    else
        fail "${ADR_FAILS} Accepted ADR(s) missing from CLAUDE.md or marked otherwise"
    fi
fi

# ──────────────────────────────────────────────────────────────────────────
# Check 6 — every public header has `#pragma once` and ends in `.hpp`.
# ──────────────────────────────────────────────────────────────────────────
echo "[6/6] Public-header guard (core/include/shader_clippy/)"
HEADERS_DIR="$REPO_ROOT/core/include/shader_clippy"
HEADER_FAILS=0
if [ ! -d "$HEADERS_DIR" ]; then
    fail "missing $HEADERS_DIR"
else
    for entry in "$HEADERS_DIR"/*; do
        [ -e "$entry" ] || continue
        # Skip subdirectories — public-header policy applies to the top
        # tier; nested sub-modules carry their own policy when introduced.
        [ -f "$entry" ] || continue
        case "$entry" in
            *.hpp)
                # `#pragma once` must appear in the file. Tolerate leading
                # whitespace + a BOM-stripping read.
                if grep -E -q '^[[:space:]]*#pragma[[:space:]]+once' "$entry"; then
                    :
                else
                    printf '    %s has no `#pragma once`\n' \
                        "${entry#"$REPO_ROOT/"}" >&2
                    HEADER_FAILS=$((HEADER_FAILS + 1))
                fi
                ;;
            *)
                printf '    %s does not end in .hpp\n' \
                    "${entry#"$REPO_ROOT/"}" >&2
                HEADER_FAILS=$((HEADER_FAILS + 1))
                ;;
        esac
    done
    if [ "$HEADER_FAILS" -eq 0 ]; then
        pass "every file under core/include/shader_clippy/ ends in .hpp + has #pragma once"
    else
        fail "${HEADER_FAILS} public-header guard violation(s)"
    fi
fi

# --- Summary ---------------------------------------------------------------
TOTAL=$((PASS_COUNT + FAIL_COUNT))
echo
echo "release-audit: ${PASS_COUNT}/${TOTAL} checks passed"
if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "release-audit: NOT READY to tag v${TAG_VERSION}" >&2
    exit 1
fi
echo "release-audit: READY to tag v${TAG_VERSION}"
exit 0
