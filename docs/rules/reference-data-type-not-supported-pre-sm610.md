---
id: reference-data-type-not-supported-pre-sm610
category: sm6_10
severity: warn
applicability: suggestion
since-version: v0.8.0
phase: 8
language_applicability: ["hlsl", "slang"]
---

# reference-data-type-not-supported-pre-sm610

> **Status:** shipped (Phase 8) — see [CHANGELOG](../../CHANGELOG.md).

## What it detects

`<qual> ref <type>` parameter syntax (matching HLSL Specs proposal 0006,
under-review) on a translation unit targeting SM 6.9 or older.

## Why it matters on a GPU

Reference data types are not yet shipped retail. Source compiled with the
proposal-0006 syntax against SM 6.9 toolchains may compile-error or
produce wrong code. The rule warns prospectively until the proposal ships.

## Options

none

## Fix availability

**suggestion** — Either bump the target to SM 6.10+ or remove the
reference parameter syntax.
