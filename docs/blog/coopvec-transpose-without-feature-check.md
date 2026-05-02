---
title: "coopvec-transpose-without-feature-check: A cooperative-vector matrix-multiply call whose `transposed` flag is true (`MATRIX_FLAG_TRANSPOSED` set) without a corresponding…"
date: 2026-05-02
author: hlsl-clippy maintainers
category: cooperative-vector
tags: [hlsl, performance, cooperative-vector]
status: stub
related-rule: coopvec-transpose-without-feature-check
---

# coopvec-transpose-without-feature-check

> **Status:** stub. The full-length analysis is queued for a v1.0.x patch
> release per [ADR 0018, section 5, criterion #6](../decisions/0018-v08-research-direction.md).
> The companion rule page at [docs/rules/coopvec-transpose-without-feature-check.md](../rules/coopvec-transpose-without-feature-check.md)
> contains the canonical detection logic + GPU reasoning.

## TL;DR

The transpose path lets the application swap the matrix's effective row/column orientation at the matmul site without re-uploading the data. On NVIDIA Ada Lovelace, the tensor cores expose hardware transpose as a free-rate option in the inference path; on AMD RDNA 3/4 WMMA, transpose support is gated behind a driver feature flag because the WMMA hardware lacks the on-the-fly swizzle and the driver emulates it via a software path; on Intel Xe-HPG XMX engines, transpose is supported but the cost varies by component type. The runtime exposes all of this through a single tier value: if the device is tier 0 or below the per-IHV transpose tier threshold, the transpose call fails.

## What the rule fires on

A cooperative-vector matrix-multiply call whose `transposed` flag is true (`MATRIX_FLAG_TRANSPOSED` set) without a corresponding application-side feature check on `D3D12_FEATURE_DATA_D3D12_OPTIONS18.CooperativeVectorTier`. The SM 6.9 cooperative-vector spec marks the transpose path as conditionally supported: tier 1 implementations may not honour it, and a runtime call with transpose set on an unsupporting device fails. The rule surfaces every transpose use so the developer can confirm the tier check is in place.

See the [What it detects](../rules/coopvec-transpose-without-feature-check.md#what-it-detects) section of
the rule page for the full pattern definition.

## Why it matters

The full GPU-mechanism analysis lives in the
[Why it matters on a GPU](../rules/coopvec-transpose-without-feature-check.md#why-it-matters-on-a-gpu)
section of the companion rule page.

## Examples

The bad / good code snippets are kept canonical on the rule page; see
[coopvec-transpose-without-feature-check.md -> Examples](../rules/coopvec-transpose-without-feature-check.md#examples).

## See also

- [Rule page](../rules/coopvec-transpose-without-feature-check.md) -- canonical reference + change log.
- [cooperative-vector overview](./ser-coop-vector-overview.md) -- broader context.
- [ADR 0018](../decisions/0018-v08-research-direction.md) -- v1.0 readiness
  plan.

---

*This is a v1.0-ship stub. Full analysis pending; track [issue link
TBD](https://github.com/NelCit/hlsl-clippy/issues).*
