# cmake/SlangVersion.cmake
#
# Single source of truth for the pinned Slang version used by this project.
#
# This must match an upstream Slang release tag at
#   https://github.com/shader-slang/slang/releases/tag/v<version>
# `tools/fetch-slang.{sh,ps1}` downloads the matching prebuilt tarball
# into `$HOME/.cache/hlsl-clippy/slang/<version>/` and `cmake/UseSlang.cmake`
# resolves that cache. Bumping the version is a one-line edit here;
# fetch-slang's next run downloads the new prebuilt automatically (the
# cache is keyed by version, so old entries are bypassed without manual
# cleanup). See external/slang-version.md for the bumping checklist.
#
# The string must NOT have a leading "v"; release-tarball URLs and cache
# directory names append the prefix where needed.
#
# This file is also parsed by `tools/fetch-slang.ps1` and
# `tools/fetch-slang.sh` with a regex against `set(HLSL_CLIPPY_SLANG_VERSION
# "<ver>")`. Keep that line single-line and quoted.

set(HLSL_CLIPPY_SLANG_VERSION "2026.7.1" CACHE STRING
    "Pinned Slang release version (downloaded by tools/fetch-slang.{sh,ps1}).")
mark_as_advanced(HLSL_CLIPPY_SLANG_VERSION)
