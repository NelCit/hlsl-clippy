#!/usr/bin/env python3
"""Generate canonical golden snapshots from `hlsl-clippy lint` output.

This script is the WRITE side of the golden-snapshot test loop; the READ
side is `tests/unit/test_golden_snapshots.cpp`. Both produce byte-identical
JSON for the same fixture.

JSON shape per fixture:

    {
      "fixture": "<basename.hlsl>",
      "diagnostics": [
        { "rule": "...", "line": N, "col": N, "severity": "...",
          "message": "..." },
        ...
      ]
    }

Sort key: (line, col, rule). Output is `json.dumps(..., indent=2)` plus a
trailing newline -- this matches `nlohmann::json::dump(2)` output formatting.

The CLI's plain-text format is:

    <path>:<line>:<col>: <severity>: <message> [<rule-id>]
      <line> | <snippet>
                ^^^^^^^^

Subsequent snippet + caret lines start with two leading spaces and a digit,
so we match the diagnostic header line by anchoring on the file path.

Usage:
    python tests/golden/normalize.py \\
        --cli build/cli/hlsl-clippy.exe \\
        --fixtures-dir tests/golden/fixtures \\
        --snapshots-dir tests/golden/snapshots
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# Group 1: line, group 2: col, group 3: severity, group 4: message,
# group 5: rule id. Anchored at start of line; the path prefix may contain
# colons on Windows (e.g. C:\...), so we accept anything up to the LAST
# `:N:N: ` before a known severity word.
_HEADER_RE = re.compile(
    r"^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+):\s+"
    r"(?P<severity>error|warning|note):\s+"
    r"(?P<message>.+)\s+\[(?P<rule>[A-Za-z0-9_\-:]+)\]\s*$"
)


def parse_cli_output(text: str) -> list[dict]:
    """Parse the CLI's plain-text diagnostic stream into a list of rows."""
    rows: list[dict] = []
    for line in text.splitlines():
        m = _HEADER_RE.match(line)
        if not m:
            # Snippet / caret / trailing summary lines fall through.
            continue
        rows.append(
            {
                "rule": m.group("rule"),
                "line": int(m.group("line")),
                "col": int(m.group("col")),
                "severity": m.group("severity"),
                "message": m.group("message").rstrip(),
            }
        )
    return rows


def canonical_dump(rows: list[dict], fixture_name: str) -> str:
    rows_sorted = sorted(rows, key=lambda r: (r["line"], r["col"], r["rule"]))
    obj = {
        "fixture": fixture_name,
        "diagnostics": rows_sorted,
    }
    return json.dumps(obj, indent=2, ensure_ascii=False) + "\n"


def run_cli(cli: Path, fixture: Path) -> str:
    # The CLI exits non-zero when diagnostics fire; that's fine -- we just
    # want stdout regardless of exit code.
    proc = subprocess.run(
        [str(cli), "lint", str(fixture)],
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return proc.stdout


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--cli", type=Path, required=True, help="path to hlsl-clippy[.exe]")
    p.add_argument("--fixtures-dir", type=Path, required=True)
    p.add_argument("--snapshots-dir", type=Path, required=True)
    args = p.parse_args()

    if not args.cli.exists():
        print(f"normalize.py: CLI not found: {args.cli}", file=sys.stderr)
        return 2
    if not args.fixtures_dir.is_dir():
        print(f"normalize.py: fixtures dir not found: {args.fixtures_dir}", file=sys.stderr)
        return 2
    args.snapshots_dir.mkdir(parents=True, exist_ok=True)

    fixtures = sorted(args.fixtures_dir.glob("*.hlsl"))
    if not fixtures:
        print(f"normalize.py: no fixtures in {args.fixtures_dir}", file=sys.stderr)
        return 2

    total = 0
    for fixture in fixtures:
        text = run_cli(args.cli, fixture)
        rows = parse_cli_output(text)
        snapshot_text = canonical_dump(rows, fixture.name)
        out_path = args.snapshots_dir / (fixture.stem + ".json")
        out_path.write_text(snapshot_text, encoding="utf-8", newline="\n")
        total += len(rows)
        print(f"  {fixture.name}: {len(rows)} diagnostic(s) -> {out_path.name}")
    print(f"normalize.py: wrote {len(fixtures)} snapshot(s); {total} total diagnostics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
