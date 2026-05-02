# Known test failures

A first-time `ctest --test-dir build` run on `main` will report
**4 failing tests** out of 672. They are tracked here so new
contributors aren't alarmed.

(Pre-`9198d48`: a fresh Windows checkout often saw 10 / 10 goldens
fail because the snapshot files carried CRLF line endings while the
in-memory `actual` was LF. `.gitattributes` now hard-pins
`tests/golden/snapshots/*.json` and `tests/golden/fixtures/*.hlsl` to
LF, and the comparison helper in `tests/unit/test_golden_snapshots.cpp`
strips `\r` defensively. Of the 10 cases, 6 were CRLF-only and now
pass; the 4 listed below are real crashes.)

## 4 × `STATUS_STACK_BUFFER_OVERRUN` in `test_golden_snapshots.cpp`

| Test case (Catch2 name) | Snapshot file | Status |
|---|---|---|
| `golden snapshot: phase2-misc` | `tests/golden/snapshots/phase2-misc.json` | crashes |
| `golden snapshot: phase3-bindings` | `tests/golden/snapshots/phase3-bindings.json` | crashes |
| `golden snapshot: phase4-atomics` | `tests/golden/snapshots/phase4-atomics.json` | crashes |
| `golden snapshot: phase4-control-flow` | `tests/golden/snapshots/phase4-control-flow.json` | crashes |

These crash with `STATUS_STACK_BUFFER_OVERRUN` (Windows) /
`SIGSEGV` (Linux/macOS). The crash is non-deterministic and only
affects 4 of 10 golden-snapshot test cases — most of the harness
runs cleanly.

### What we know

- The harness in `tests/unit/test_golden_snapshots.cpp` runs `lint()`
  against `tests/golden/fixtures/<phase>-<topic>.hlsl`, marshals the
  diagnostics to canonical JSON, and compares against
  `tests/golden/snapshots/<phase>-<topic>.json`. The crash happens
  inside `lint()`, not the JSON comparison — most likely either:
  - a per-rule deep recursion blowing the stack on one of the larger
    phase fixtures, OR
  - a parser-bridge / tree-sitter cursor walk depth issue that only
    surfaces on certain fixture shapes.
- The 4 historical rule bugs identified during Phase 4 wiring
  (`gather-channel-narrowing` 0x01 control byte,
  `redundant-precision-cast` not firing, `compare-equal-float`
  misses `==`, cbuffer-rules grammar gap) are unrelated; those were
  fixed during the v0.5 launch chain.
- Pre-existing in v0.5.0 — the failures predate every audit and tag
  in the v0.5 series.

### Tracking

- `ROADMAP.md` status banner notes "667/671" baseline.
- `CHANGELOG.md` `[0.5.4]` entry mentions the v0.6 hardening backlog.
- A dedicated GitHub issue is **TODO** (will be filed pre-v0.6).

### Workarounds

For local development, ctest treats the 4 crashes as failures but
they don't poison the rest of the suite. Either:

```sh
ctest --test-dir build --output-on-failure --label-exclude golden
```

(if/when the golden tests grow a `golden` ctest label — see
`tests/CMakeLists.txt`), or filter by Catch2 tag:

```sh
./build/tests/hlsl_clippy_unit_tests \
  '[!.]'                       # all tests not tagged hidden, OR
  '~[golden-snapshot]'         # exclude only golden tests
```

CI does NOT exclude these tests today — they're allowed to fail and
the rest of the suite is what gates merges. This is acceptable
because the 4 cases share a single root-cause class (stack-corruption
in the lint pipeline on specific fixture shapes), and triaging them
is a v0.6 deliverable.

## Anything else?

If you hit a *fifth* test failure on a fresh `main` build, that's
real — please file a GitHub issue with:

- Your platform + compiler version
- The full `ctest --output-on-failure` log
- The contents of any `.actual` files written next to
  `tests/golden/snapshots/`
