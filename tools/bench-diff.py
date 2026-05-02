#!/usr/bin/env python3
"""Diff two Catch2 XML bench reports and emit a markdown summary.

Used by `.github/workflows/bench.yml`'s "Compare to previous nightly"
step, but writeable to STDOUT for local triage runs (e.g. when bisecting
a regression by hand against an artifact downloaded via `gh run download`).

Output is a GitHub-flavoured markdown table sorted by absolute delta so the
first row is always the most-changed benchmark. Unchanged rows are still
listed to make it obvious when a regression is *not* present.

Usage:
    bench-diff.py --current bench-current.xml --previous bench-prev.xml
    bench-diff.py --current bench-current.xml         (no previous: just lists)

Exit codes:
    0 — diff succeeded, regardless of whether anything regressed
    2 — failed to parse one of the inputs
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass


# Threshold percentages for the delta column's emoji indicator. Catch2's
# bench numbers are noisy on shared GHA runners (5-10% std-dev is normal),
# so don't flag anything under 10%.
WARN_PCT = 10.0
ALERT_PCT = 25.0


@dataclass
class Result:
    name: str
    mean_ns: float
    stddev_ns: float

    @property
    def stddev_pct(self) -> float:
        return (self.stddev_ns / self.mean_ns * 100.0) if self.mean_ns > 0 else 0.0


def parse(path: str) -> dict[str, Result]:
    """Return {benchmark_name: Result} parsed from a Catch2 XML report.

    Catch2's XML schema nests <BenchmarkResults> under <TestCase>; the same
    `name` attribute appears on both. We key the dict by the BenchmarkResults
    name so the same parser handles fixtures that happen to share a TestCase
    with multiple BENCHMARKs.
    """
    tree = ET.parse(path)
    out: dict[str, Result] = {}
    for br in tree.iter("BenchmarkResults"):
        name = br.attrib.get("name")
        if name is None:
            continue
        mean_el = br.find("mean")
        std_el = br.find("standardDeviation")
        if mean_el is None or std_el is None:
            continue
        try:
            mean_ns = float(mean_el.attrib.get("value", "0"))
            std_ns = float(std_el.attrib.get("value", "0"))
        except ValueError:
            continue
        out[name] = Result(name=name, mean_ns=mean_ns, stddev_ns=std_ns)
    return out


def fmt_ns(ns: float) -> str:
    """Pretty-print a nanosecond duration with the right unit."""
    if ns >= 1_000_000:
        return f"{ns / 1_000_000:.2f} ms"
    if ns >= 1_000:
        return f"{ns / 1_000:.2f} us"
    return f"{ns:.0f} ns"


def emoji(delta_pct: float) -> str:
    """Indicator for the delta column. Slower = warning, faster = info."""
    if delta_pct >= ALERT_PCT:
        return "🔴"  # noqa: RUF001
    if delta_pct >= WARN_PCT:
        return "🟡"  # noqa: RUF001
    if delta_pct <= -ALERT_PCT:
        return "🟢"  # noqa: RUF001 — speedup
    if delta_pct <= -WARN_PCT:
        return "🟢"  # noqa: RUF001
    return "·"


def render(current: dict[str, Result], previous: dict[str, Result] | None) -> str:
    lines: list[str] = []
    lines.append("## Bench results")
    lines.append("")
    if previous is None:
        lines.append("_No previous nightly artifact found; baseline run only._")
        lines.append("")
        lines.append("| Benchmark | Mean | Std-dev |")
        lines.append("|---|---:|---:|")
        for name in sorted(current):
            r = current[name]
            lines.append(f"| `{name}` | {fmt_ns(r.mean_ns)} | {r.stddev_pct:.1f}% |")
        return "\n".join(lines) + "\n"

    rows = []
    for name in sorted(current):
        cur = current[name]
        prev = previous.get(name)
        if prev is None or prev.mean_ns <= 0:
            rows.append((float("inf"), name, cur, None, None))
            continue
        delta_pct = (cur.mean_ns - prev.mean_ns) / prev.mean_ns * 100.0
        rows.append((abs(delta_pct), name, cur, prev, delta_pct))

    # Catch benchmarks the previous run had but this run dropped — surface
    # them at the top of the regression section so a removed bench doesn't
    # silently disappear from CI tracking.
    dropped = sorted(set(previous) - set(current))

    rows.sort(key=lambda r: -r[0])  # largest absolute delta first

    lines.append(
        f"Comparison vs previous nightly. Threshold for ⚠ is ±{WARN_PCT:.0f}% mean,"
        f" ±{ALERT_PCT:.0f}% for 🔴/🟢."
    )
    lines.append("")
    lines.append("| Δ | Benchmark | Current mean | Previous mean | Δ% | Cur σ |")
    lines.append("|:-:|---|---:|---:|---:|---:|")
    for _, name, cur, prev, delta in rows:
        if prev is None:
            lines.append(f"| 🆕 | `{name}` | {fmt_ns(cur.mean_ns)} | — | new | {cur.stddev_pct:.1f}% |")
        else:
            lines.append(
                f"| {emoji(delta)} | `{name}` | {fmt_ns(cur.mean_ns)} |"
                f" {fmt_ns(prev.mean_ns)} | {delta:+.1f}% | {cur.stddev_pct:.1f}% |"
            )
    if dropped:
        lines.append("")
        lines.append("**Benchmarks removed since last run:**")
        for name in dropped:
            lines.append(f"- `{name}`")
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--current", required=True, help="Catch2 XML report from this run")
    p.add_argument("--previous", help="Catch2 XML report from the previous run (optional)")
    p.add_argument(
        "--output",
        help="Write markdown to this path (default: stdout). Pass $GITHUB_STEP_SUMMARY to "
        "post directly to the workflow summary.",
    )
    args = p.parse_args(argv)

    try:
        current = parse(args.current)
    except (ET.ParseError, FileNotFoundError) as exc:
        print(f"failed to parse --current {args.current}: {exc}", file=sys.stderr)
        return 2

    previous = None
    if args.previous:
        try:
            previous = parse(args.previous)
        except (ET.ParseError, FileNotFoundError) as exc:
            print(
                f"warning: failed to parse --previous {args.previous}: {exc}; "
                f"falling back to baseline-only mode",
                file=sys.stderr,
            )

    md = render(current, previous)
    if args.output:
        with open(args.output, "a", encoding="utf-8") as f:
            f.write(md)
    else:
        print(md, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
