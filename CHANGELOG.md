# Changelog

All notable changes to this project will be documented in this file. Format
follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Directory layout: `cli/` (CLI binary) and `core/` (library), replacing the
  earlier `crates/` placeholder.
- Modular CMake build: `hlsl_clippy_core` static library target and
  `hlsl-clippy` CLI executable target.
- Test corpus under `tests/corpus/`: 17 permissively-licensed HLSL shaders.
- Test fixtures under `tests/fixtures/`: expected diagnostics for Phase 2, 3,
  and 4 rule validation.
- GitHub Actions CI workflows: build matrix (Windows/Linux), lint gate, and
  CodeQL stub.
- Slang vendored as a git submodule with a CMake smoke test.
- tree-sitter and tree-sitter-hlsl vendored with a CMake smoke test.
- Documentation scaffolding: `docs/` tree, governance files
  (`CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`), and GitHub issue/PR
  templates.

### Changed

- Switched from MIT to Apache-2.0 license; added NOTICE and
  THIRD_PARTY_LICENSES.md.
