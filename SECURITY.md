# Security Policy

## Reporting a vulnerability

**Preferred channel:** [GitHub's private vulnerability reporting](https://github.com/NelCit/hlsl-clippy/security/advisories/new) —
this creates a private advisory visible only to maintainers and you.

If you cannot use GitHub advisories, contact the maintainer privately
at <vin.legrand11@gmail.com> with subject prefix `[hlsl-clippy security]`.

**Do not open a public GitHub issue for security reports.**

We aim to acknowledge all reports within 5 business days.

## Disclosure policy

This project follows a 90-day coordinated disclosure standard. We will
work with reporters to understand and remediate confirmed vulnerabilities
before public disclosure. Extensions may be granted by mutual agreement
when a fix is non-trivial or upstream-coordination is required.

Once a fix ships, the GitHub Security Advisory is published with
attribution (unless the reporter requests anonymity), and the fix is
called out in `CHANGELOG.md` under the affected version's
`### Fixed` section.

## Supported versions

| Version  | Supported     |
| -------- | ------------- |
| `0.5.x`  | Yes — current |
| `< 0.5`  | No            |

Security fixes ship via patch releases (e.g. `v0.5.4`) rather than the
unreleased main branch. The current shipped version is reflected in
`core/src/version.cpp`.

## Scope

**In scope:**
- The `hlsl-clippy` CLI binary
- The `hlsl-clippy-lsp` LSP server binary
- The VS Code extension under `vscode-extension/`
- The rule engine (`core/`) including the parser bridge and reflection
  bridge
- `tools/fetch-slang.{sh,ps1}` and CI workflow files (supply-chain
  attack vectors)

**Out of scope:**
- Vulnerabilities in Slang itself — report to
  <https://github.com/shader-slang/slang/security>
- Vulnerabilities in tree-sitter / tree-sitter-hlsl — report upstream
- Vulnerabilities in nlohmann/json, toml++, or other vendored deps —
  report upstream
- Issues that require a malicious workspace's `.hlsl-clippy.toml` to
  exploit a user opening that workspace (treated as social-engineering
  at the editor / CI layer, not the linter's responsibility)

## Threat model

`hlsl-clippy` parses HLSL source code that may be attacker-controlled
(e.g. a downloaded shader, a third-party submodule). The linter must
not be exploitable by parsing such input.

Known limitations as of v0.5.x (tracked for hardening):

- **No upper bound on input file size or Slang invocation time.** A
  pathological shader can consume large amounts of memory or CPU. Caps
  land in v0.5.4.
- **Slang prebuilt downloads are not signature-verified end-to-end.**
  Tarball checksums land in v0.5.4 (`HLSL_CLIPPY_SLANG_SHA256_*` in
  `cmake/SlangVersion.cmake`).
- **The LSP runs unsandboxed in the editor process tree** — standard
  for VS Code language servers. Treat `hlsl-clippy-lsp` as you would
  any compiler frontend you invoke on attacker-supplied source.
- **No fuzz harness yet.** Tracked for v0.7+ per ROADMAP Phase 7.

## Hardening backlog

- libFuzzer harness over `parser::parse`, `Config::load_config_string`,
  the suppression scanner, and the LSP framing layer.
- CodeQL workflow with `cpp` + `javascript` query packs.
- `npm audit --audit-level=high` step in CI.
- `seccomp` / Windows AppContainer sandbox for `hlsl-clippy-lsp`.
- SBOM emission alongside each release archive.
