# cmake/SlangVersion.cmake
#
# Single source of truth for the pinned Slang version used by this project.
#
# This must match the SHA checked out in `external/slang/` (currently the
# `v${HLSL_CLIPPY_SLANG_VERSION}` upstream tag, see external/slang-version.md).
# Bumping the submodule is a deliberate two-step act: update the submodule
# pointer AND update the version string here. The cache key in
# `cmake/UseSlang.cmake` is derived from this variable, so a mismatch
# invalidates any pre-populated prebuilt cache automatically.
#
# The string must NOT have a leading "v"; release-tarball URLs and cache
# directory names append the prefix where needed.
#
# This file is also parsed by `tools/fetch-slang.ps1` and
# `tools/fetch-slang.sh` with a regex against `set(HLSL_CLIPPY_SLANG_VERSION
# "<ver>")`. Keep that line single-line and quoted.

set(HLSL_CLIPPY_SLANG_VERSION "2026.7.1" CACHE STRING
    "Pinned Slang release version (matches external/slang submodule SHA).")
mark_as_advanced(HLSL_CLIPPY_SLANG_VERSION)
