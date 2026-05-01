# Golden snapshot regression tests

This directory holds the regression net for the rule-pack. Snapshot inputs are
HLSL fixtures under `fixtures/`; snapshot outputs are deterministic JSON files
under `snapshots/` recording every diagnostic the linter emits today.

A snapshot diff means a rule's behaviour changed -- review carefully before
refreshing. The most common drift sources, in order of frequency:

1. A new rule was added and now fires on existing fixtures (expected; refresh).
2. A rule's message was reworded (expected; refresh).
3. A rule started firing where it shouldn't, or stopped firing where it
   should (regression; investigate before refreshing).

The C++ test driver lives at
[`tests/unit/test_golden_snapshots.cpp`](../unit/test_golden_snapshots.cpp);
it tags every test with `[golden]` so you can opt out of the slow snapshot
walk during inner-loop work via `ctest -E golden`.

## File layout

```
tests/golden/
  fixtures/                 input HLSL with `// HIT(...)` annotations
    phase2-math.hlsl
    phase2-redundancy.hlsl
    ... (10 fixtures total today)
  snapshots/                one canonical JSON per fixture
    phase2-math.json
    phase2-redundancy.json
    ...
  README.md                 this file
```

## JSON shape

Each snapshot file holds one object per fixture, with diagnostics sorted by
`(line, col, rule)` so reorderings inside the engine never produce spurious
diffs:

```json
{
  "fixture": "phase2-math.hlsl",
  "diagnostics": [
    { "rule": "lerp-extremes", "line": 37, "col": 12,
      "severity": "warning",
      "message": "`lerp(a, b, 0)` always returns `a` ..." },
    ...
  ]
}
```

Encoding is UTF-8 without BOM, LF line endings, two-space indent, trailing
newline -- the exact byte format `nlohmann::json::dump(2)` emits with the
default `ensure_ascii = false`. The C++ test driver and the
`tools/update-goldens.{ps1,sh}` regenerators both produce this format.

## How to add a new fixture

1. Pick (or hand-write) an HLSL file rich in `// HIT(rule-id)` /
   `// SHOULD-NOT-HIT(rule-id)` annotations. Drop it in `fixtures/` with an
   ASCII-only filename matching `phaseN-<topic>.hlsl`.
2. Add a `TEST_CASE("golden snapshot phaseN-<topic>", "[golden]") { ... }`
   block to `tests/unit/test_golden_snapshots.cpp` that calls
   `run_one_fixture(...)` against the fixture / snapshot path pair.
3. Regenerate the snapshot:
   ```sh
   pwsh tools/update-goldens.ps1            # Windows
   bash tools/update-goldens.sh             # Linux / macOS
   ```
   or, after building, set `HLSL_CLIPPY_GOLDEN_UPDATE=1` and re-run the
   `[golden]` tests directly via ctest -- the C++ driver supports both
   read-and-compare and write modes.
4. Inspect `snapshots/<basename>.json`; sanity-check the line/col numbers
   against the fixture's `// HIT(...)` annotations.
5. Commit fixture + snapshot together using:
   ```
   test(golden): add <topic> fixture
   ```

## How to refresh snapshots after a rule change

If you changed a rule's behaviour or message and the snapshot test now fails
in CI:

1. Build `hlsl-clippy` (the CLI target is what update-goldens uses).
2. Run the regenerator:
   ```sh
   pwsh tools/update-goldens.ps1
   bash tools/update-goldens.sh
   ```
3. `git diff tests/golden/snapshots/` -- read every line. The diff IS the
   review surface; if a change you didn't intend appears, that's a bug, not
   a snapshot to refresh.
4. Commit:
   ```
   test(golden): refresh snapshots after <rule-id> change
   ```

## On mismatch (CI-side workflow)

When a `[golden]` test fails it writes the actual output next to the
expected file with the suffix `.actual`:

```
tests/golden/snapshots/phase2-math.json          (expected)
tests/golden/snapshots/phase2-math.json.actual   (actual)
```

Local triage: `diff` the two files. The `.actual` file is git-ignored; once
the diff is reviewed and accepted, `mv` it over the expected file (or
re-run the regenerator) and commit.

## Why two write paths?

The C++ test driver writes via the `lint()` API + nlohmann/json -- this is
what drives the regression test in CI. The shell regenerators
(`tools/update-goldens.{ps1,sh}`) wrap the existing CLI plain-text output
and parse it. Both produce byte-identical JSON so the shell tools can be
used pre-CMake-rebuild for quick triage; the C++ driver is the source of
truth in CI.

The bash regenerator embeds an inline Python post-processor; a stand-alone
copy lives at [`normalize.py`](normalize.py) for users who prefer to invoke
the post-processor by hand:

```sh
python tests/golden/normalize.py \
    --cli build/cli/hlsl-clippy.exe \
    --fixtures-dir tests/golden/fixtures \
    --snapshots-dir tests/golden/snapshots
```
