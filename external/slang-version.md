# Slang prebuilt pin

## Tag

`v2026.7.1` — latest stable release as of 2026-04-30.

GitHub release page: <https://github.com/shader-slang/slang/releases/tag/v2026.7.1>

This version is pinned in `cmake/SlangVersion.cmake`:

```cmake
set(SHADER_CLIPPY_SLANG_VERSION "2026.7.1" ...)
```

## How Slang reaches the build

The `external/slang` git submodule was retired in 2026-05; from-source
Slang builds were costing every CI job and every fresh worktree ~20
minutes of cold compile, with no measurable upside over the upstream
prebuilt tarballs that Shader-Slang ships for the same tagged release.

Resolution order in `cmake/UseSlang.cmake`:

1. `Slang_ROOT` — explicit prefix (escape hatch for power users who
   need a custom Slang build).
2. **Per-user prebuilt cache** — populated by
   `tools/fetch-slang.{sh,ps1}`. Cache root:
   - Windows: `%LOCALAPPDATA%/shader-clippy/slang/<version>/`
   - Linux/macOS: `$HOME/.cache/shader-clippy/slang/<version>/`

If neither resolves, the configure step fails with a `FATAL_ERROR`
that prints the exact `tools/fetch-slang.{sh,ps1}` command to run.

## Bumping the Slang version

1. Edit `SHADER_CLIPPY_SLANG_VERSION` in `cmake/SlangVersion.cmake`.
   The fetch scripts parse this line with a regex; keep it on one
   line and quoted.
2. Run `tools/fetch-slang.{sh,ps1}` locally to populate the new
   cache entry and verify the build passes.
3. CI will fetch the new version on its next run automatically; the
   cache is keyed by version so the old entry is bypassed without
   manual cleanup.

## Power user: from-source / custom builds

If you ever need a Slang version that has no prebuilt tarball (e.g.
an unreleased commit with a critical fix, or a custom fork), build
Slang yourself against the upstream repo and point at the install
prefix:

```sh
cmake -B build -DSlang_ROOT=/path/to/your/slang/install
```

The `cmake/UseSlang.cmake` `Slang_ROOT` path is unchanged from the
submodule era — only the in-tree `external/slang` submodule was
removed, not the underlying integration capability. Upstream's
build prerequisites are documented at
<https://github.com/shader-slang/slang/blob/master/docs/building.md>.

The previous build flags that were forced when we built Slang in-tree
(`SLANG_ENABLE_TESTS=OFF`, `SLANG_ENABLE_GFX=OFF`, etc.) live with
your local Slang build now if you go this route — pass the same
`-DSLANG_ENABLE_*=OFF` flags when configuring upstream Slang to keep
the build narrow.
