# cmake/TreeSitterSlangVersion.cmake
#
# Single source of truth for the pinned tree-sitter-slang grammar SHA used by
# this project (ADR 0021, sub-phase B.1 — v1.4.0).
#
# Upstream: https://github.com/tree-sitter-grammars/tree-sitter-slang
#
# This grammar extends tree-sitter-hlsl (it imports HLSL's grammar.js verbatim
# and adds Slang-specific productions on top), so node-kind names for HLSL-
# common constructs are preserved by construction. ADR 0021 §1 originally
# named `Theta-Dev/tree-sitter-slang` as the only candidate; that repo no
# longer exists upstream as of 2026-05-02. The community
# `tree-sitter-grammars/tree-sitter-slang` repository is a strict superset
# upgrade — it inherits HLSL's grammar instead of forking-and-diverging from
# C, which is why we adopt it as the v1.4.0 base.
#
# Pin to a specific commit SHA (not a branch); upstream tags are mutable.
# Bump procedure: pick a new SHA from the upstream `master` branch, drop it
# in below, and update the submodule pointer with
#   `git -C external/tree-sitter-slang fetch origin && git -C external/tree-sitter-slang checkout <SHA>`
# from the repo root.
#
# Maintainer follow-up (per ADR 0021's Option B vs A reconciliation): the
# v1.4.0 ship vendors upstream as-is (Option A's effective pattern). Forking
# into `nelcit/tree-sitter-slang` is queued for v1.4.0.x — we don't have
# GitHub-auth scope from inside the implementation agent, so the maintainer
# performs the fork later and points this submodule at the fork.

set(SHADER_CLIPPY_TREE_SITTER_SLANG_SHA
    "1dbcc4abc7b3cdd663eb03d93031167d6ed19f56"
    CACHE STRING
    "Pinned tree-sitter-slang grammar commit SHA (tree-sitter-grammars/tree-sitter-slang).")
mark_as_advanced(SHADER_CLIPPY_TREE_SITTER_SLANG_SHA)
