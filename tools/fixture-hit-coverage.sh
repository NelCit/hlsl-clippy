#!/usr/bin/env bash
# fixture-hit-coverage.sh
#
# For every `// HIT(rule)` annotation in tests/fixtures/, verify the named
# rule actually fires on that fixture via the CLI (shares the core/ library
# with the LSP server, so a CLI hit means the LSP server delivers the same).
#
# Excludes:
#   - banner placeholder `// HIT(rule-id): reason` (documentation example)
#   - `// HIT(name)` where `name` looks like a placeholder (rule-id, foo, bar)
#
# Usage: ./tools/fixture-hit-coverage.sh
# Exit:  0 if every annotation fires, 1 otherwise.

set -uo pipefail

CLI="${SHADER_CLIPPY_CLI:-./build/cli/shader-clippy.exe}"
[[ ! -x "$CLI" ]] && CLI="${SHADER_CLIPPY_CLI:-./build/cli/shader-clippy}"
if [[ ! -x "$CLI" ]]; then
    echo "ERROR: shader-clippy CLI not found at $CLI" >&2
    exit 2
fi

PLACEHOLDERS='^(rule-id|foo|bar|baz)$'

total=0
matched=0
declare -a misses=()

while IFS= read -r -d '' fixture; do
    annots=$(grep -oE '// HIT\([a-z0-9-]+\)' "$fixture" | grep -oE '\([a-z0-9-]+\)' | tr -d '()' | sort -u)
    [[ -z "$annots" ]] && continue
    diag_json=$("$CLI" lint --format=json "$fixture" 2>/dev/null)
    fired=$(printf '%s' "$diag_json" | grep -oE '"rule":"[^"]+"' | grep -oE '"[^"]+"$' | tr -d '"' | sort -u)
    while IFS= read -r rule; do
        [[ -z "$rule" ]] && continue
        [[ "$rule" =~ $PLACEHOLDERS ]] && continue
        ((total++))
        if printf '%s\n' "$fired" | grep -qx "$rule"; then
            ((matched++))
        else
            misses+=("$rule  →  $(realpath --relative-to=. "$fixture")")
        fi
    done <<< "$annots"
done < <(find tests/fixtures -type f \( -name "*.hlsl" -o -name "*.slang" \) -print0)

missed=$((total - matched))

echo "=== Fixture HIT coverage (CLI default flags) ==="
echo "  total HIT-rule pairs:  $total"
echo "  matched:               $matched"
echo "  missed:                $missed"
if (( missed > 0 )); then
    echo
    echo "Missing pairs:"
    printf '  %s\n' "${misses[@]}" | sort -u
    exit 1
fi
exit 0
