# Slang golden snapshots (ADR 0020 sub-phase A)

Reserved directory for v1.3+ Slang-source golden snapshots. The
`tests/unit/test_golden_snapshots.cpp` harness filters out `clippy::*`
infrastructure diagnostics by design, so a golden snapshot of a `.slang`
fixture under the current rule set would mostly be empty (~32 of 189 rules
fire on Slang, and most of the public fixtures don't trip them). Wiring
real Slang-source goldens lands together with sub-phase B (tree-sitter-slang)
when AST + CFG rules also start firing on `.slang` and the snapshot matrix
becomes worth the maintenance.

For v1.3.0, the dispatch + skip-notice + AST-skip behaviour is locked in by
`tests/unit/test_slang_dispatch.cpp`.
