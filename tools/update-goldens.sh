#!/usr/bin/env bash
# tools/update-goldens.sh -- regenerate golden-snapshot JSON.
#
# Wraps `cmake --build` against the CLI target, then runs the CLI on every
# fixture under `tests/golden/fixtures/` and normalises the plain-text
# output into canonical sorted JSON under `tests/golden/snapshots/`.
#
# The C++ test driver `tests/unit/test_golden_snapshots.cpp` produces the
# same canonical bytes via the C++ API; the two paths must agree.
#
# Usage:
#   bash tools/update-goldens.sh                        # default (./build)
#   bash tools/update-goldens.sh --build-dir build-rel  # explicit dir
#   bash tools/update-goldens.sh --skip-build           # skip cmake --build

set -euo pipefail

build_dir="build"
skip_build=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --skip-build) skip_build=1; shift ;;
        *) echo "update-goldens.sh: unknown arg: $1" >&2; exit 2 ;;
    esac
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixtures_dir="$repo_root/tests/golden/fixtures"
snapshots_dir="$repo_root/tests/golden/snapshots"

# CLI exe lives at build/cli/shader-clippy on Linux/macOS, .exe on Windows.
cli_exe="$repo_root/$build_dir/cli/shader-clippy"
if [[ ! -x "$cli_exe" && -x "$cli_exe.exe" ]]; then
    cli_exe="$cli_exe.exe"
fi

if [[ "$skip_build" -ne 1 ]]; then
    echo "[update-goldens] cmake --build $build_dir --target shader-clippy"
    cmake --build "$build_dir" --target shader-clippy
fi

if [[ ! -x "$cli_exe" ]]; then
    echo "update-goldens.sh: CLI not found: $cli_exe" >&2
    exit 2
fi
if [[ ! -d "$fixtures_dir" ]]; then
    echo "update-goldens.sh: fixtures dir not found: $fixtures_dir" >&2
    exit 2
fi
mkdir -p "$snapshots_dir"

# Inline Python is the most portable JSON serialiser; falls back to python3.
python_exe="$(command -v python3 || command -v python || true)"
if [[ -z "$python_exe" ]]; then
    echo "update-goldens.sh: requires python3 (or python) for JSON serialisation" >&2
    exit 2
fi

# The Python helper parses the CLI plain-text output and emits canonical
# JSON matching `nlohmann::json::dump(2)` byte-for-byte. Embedded as a
# heredoc so the shell wrapper is single-file.
total=0
count=0
for fixture in "$fixtures_dir"/*.hlsl; do
    [[ -e "$fixture" ]] || continue
    name="$(basename "$fixture")"
    stem="${name%.hlsl}"
    out_path="$snapshots_dir/$stem.json"

    # The CLI exits non-zero when diagnostics fire; tolerate it.
    cli_text="$("$cli_exe" lint "$fixture" 2>/dev/null || true)"

    diag_count="$(printf '%s' "$cli_text" | "$python_exe" - "$name" "$out_path" <<'PY'
import json, re, sys

fixture_name = sys.argv[1]
out_path = sys.argv[2]
text = sys.stdin.read()

header_re = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+):\s+"
    r"(?P<severity>error|warning|note):\s+"
    r"(?P<message>.+)\s+\[(?P<rule>[A-Za-z0-9_\-:]+)\]\s*$"
)

rows = []
for line in text.splitlines():
    m = header_re.match(line)
    if not m:
        continue
    rows.append({
        "rule": m.group("rule"),
        "line": int(m.group("line")),
        "col": int(m.group("col")),
        "severity": m.group("severity"),
        "message": m.group("message").rstrip(),
    })

rows.sort(key=lambda r: (r["line"], r["col"], r["rule"]))
obj = {"fixture": fixture_name, "diagnostics": rows}
# `ensure_ascii=False` matches nlohmann's default (UTF-8 passthrough).
# `indent=2` matches `dump(2)`. Trailing newline keeps the file POSIX-y.
with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
    fh.write(json.dumps(obj, indent=2, ensure_ascii=False))
    fh.write("\n")
print(len(rows))
PY
    )"
    echo "  $name: $diag_count diagnostic(s) -> $stem.json"
    total=$((total + diag_count))
    count=$((count + 1))
done

echo "[update-goldens] wrote $count snapshot(s); $total total diagnostics"
