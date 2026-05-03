# cmake/NlohmannJsonVersion.cmake
#
# Single source of truth for the pinned nlohmann/json version used by the
# LSP frontend (per ADR 0014). Mirrors cmake/SlangVersion.cmake's pattern.
#
# This must match the tag checked out in `external/nlohmann_json/`. Bumping
# the submodule is a deliberate two-step act: update the submodule pointer
# AND update the version string here.

set(SHADER_CLIPPY_NLOHMANN_JSON_VERSION "3.12.0" CACHE STRING "Pinned nlohmann/json version")
mark_as_advanced(SHADER_CLIPPY_NLOHMANN_JSON_VERSION)
