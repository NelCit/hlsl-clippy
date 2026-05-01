# hlsl-clippy lint-perf bench

Catches per-rule / per-file `lint()` regressions before they reach a release.
Walks the public 27-shader corpus under [`tests/corpus/`](../corpus/) and
reports median + stddev timings for each file plus an aggregate "lint full
corpus" rollup. Built on Catch2 v3's built-in benchmarking support
(`BENCHMARK` macro, gated by `CATCH_CONFIG_ENABLE_BENCHMARKING`).

The bench is **NOT** a unit test. It does not run under `ctest`; it lives in
its own executable target (`hlsl_clippy_bench`) that is only built in
`Release` or `RelWithDebInfo` configurations — Debug numbers are noise.

---

## Build

The bench is excluded from Debug builds at CMake-configure time, so just
configure with a release-grade build type:

```sh
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target hlsl_clippy_bench
```

For a multi-config generator (Visual Studio, Xcode) the target is always
defined; pick the config at build time:

```sh
cmake -B build-msvc -G "Visual Studio 17 2022"
cmake --build build-msvc --config Release --target hlsl_clippy_bench
```

Windows: the build copies Slang's seven runtime DLLs next to the exe via the
shared `hlsl_clippy_deploy_slang_dlls` helper, so the bench is launchable
without `PATH` adjustments.

## Run

```sh
# Run every benchmark in the binary (per-file + full-corpus rollup).
./build-release/tests/bench/hlsl_clippy_bench

# Run only one named benchmark — useful when bisecting a regression.
./build-release/tests/bench/hlsl_clippy_bench "lint sponza-vert"
./build-release/tests/bench/hlsl_clippy_bench "lint full corpus"

# List the available benchmarks without running them.
./build-release/tests/bench/hlsl_clippy_bench --list-tests --tags "[bench]"
```

The benchmark name format is `lint <stem>` where `<stem>` is the fixture's
basename without extension (so `compute/gaussian_blur_cs.hlsl` →
`lint gaussian_blur_cs`).

Catch2 takes 100 samples per benchmark by default; override with
`--benchmark-samples N`. Increase to 500 for a publication-quality CI run,
drop to 20 for fast local sanity checks.

## Interpret

Catch2 prints a table that looks roughly like this (numbers fabricated):

```
benchmark name             samples    iterations    estimated
                           mean       low mean      high mean
                           std dev    low std dev   high std dev
-----------------------------------------------------------------
lint gaussian_blur_cs      100        1             182.4 ms
                           1.83 ms    1.79 ms       1.88 ms
                           24.6 us    18.3 us       38.1 us
```

The headline numbers are:

- **`mean`** — the per-file mean time. This is what CI compares against
  the baseline. If a single file's mean jumps **>20%** across commits,
  treat it as a regression and bisect.
- **`std dev`** — noise floor. If `std dev / mean > 5%`, the run is too
  noisy to trust; re-run after closing background apps, or bump
  `--benchmark-samples`.
- **`lint full corpus`** — total time for one pass over all 27 fixtures,
  one iteration per sample. This is the rollup the nightly CI dashboard
  watches; it amortises per-file noise.

### What "regression" looks like

A new rule that adds a tree-sitter walk over the whole AST tends to add
~5–15 us per file. That is fine. A new rule that re-parses the source per
hit, or that allocates per-AST-node, will balloon a single fixture by
hundreds of microseconds — that's the signal the per-file table catches
and the rollup misses.

### What this bench does NOT measure

- **Reflection / Slang-IR cost.** The `make_default_rules()` pack invokes
  reflection lazily on Phase 3 rules; the first iteration on a given file
  pays the cold cost, subsequent iterations hit the per-`(SourceId,
  profile)` cache. Catch2's iteration count masks this — see
  `LintOptions.enable_reflection` in `core/include/hlsl_clippy/lint.hpp`
  if you want to bench AST-only.
- **CFG cost.** Same caveat; `LintOptions.enable_control_flow` toggles the
  Phase 4 stage.
- **Suppression / config parse.** The bench does not load
  `.hlsl-clippy.toml`; it runs every rule at default severity.

If a regression hits and the per-file numbers are stable but the rollup
isn't, suspect a registry-walk cost (`make_default_rules()` allocation),
not an individual rule.

---

## CI integration (TODO — not in this PR)

The bench is **not** wired into `ci.yml`. Per-PR builds skip it (Debug
config, and the bench won't build under Debug anyway). The next step is a
new `.github/workflows/bench.yml` that:

1. Runs nightly on `cron: '17 2 * * *'`.
2. Configures `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo` on the
   `windows-2022` + `ubuntu-24.04` matrix.
3. Builds `hlsl_clippy_bench`.
4. Runs `./hlsl_clippy_bench --reporter=xml --out=bench.xml
   --benchmark-samples=200`.
5. Uploads `bench.xml` as an artifact.
6. (Stretch) compares against the previous night via a `bench-history`
   action and posts a delta to a tracking issue.

Owner of follow-up: orchestrator. Don't add `hlsl_clippy_bench` to
`ci.yml` — its run-time exceeds the per-PR budget.
