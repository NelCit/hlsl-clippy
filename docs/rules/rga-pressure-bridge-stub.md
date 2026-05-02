---
id: rga-pressure-bridge-stub
category: rdna4
severity: note
applicability: none
since-version: v0.8.0
phase: 8
---

# rga-pressure-bridge-stub

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

Per ADR 0018 §4.3, this is an infrastructure investment, not a rule per se.
Fires once per source compiled under the `[experimental.target = rdna4]`
config gate, emitting a `Severity::Note` informational diagnostic that
points the developer to the future `tools/rga-bridge` work item.

## Why it matters on a GPU

The current `vgpr-pressure-warning` rule is an AST heuristic. AMD's RGA
("Live VGPR Analysis") produces ground-truth per-block VGPR counts after
codegen. The bridge is queued for v0.10; this stub keeps the candidate
alive in the rule catalog while the infrastructure is being built.

## Options

none. Activated only under `[experimental] target = "rdna4"`.

## Fix availability

**none** — Informational note.

## See also

- ADR 0018 §4.3
- AMD GPUOpen: Live VGPR Analysis with RGA
