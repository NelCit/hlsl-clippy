# Release checklist

Pre-tag checklist for cutting a `v*.*.*` release. Run through every box
before pushing the tag — once the tag is in `origin`, both
`.github/workflows/release.yml` (binary artifacts) and
`.github/workflows/release-vscode.yml` (Marketplace `.vsix`) fire
automatically and there is no clean rollback for a pushed tag.

ADR cross-reference: ADR 0014 §"Sub-phase 5e — Distribution".

## 1. Bump version strings (must match across all surfaces)

The Marketplace `package.json` version is verified against the tag in CI; a
mismatch fails the workflow. The other surfaces are best-effort consistency
— bump them in the same commit so reviewers can grep the bump trivially.

- [ ] `core/src/version.cpp` — update the literal returned by
      `hlsl_clippy::version()`. The CLI's `--version` reads through here.
- [ ] `vscode-extension/package.json` — `"version": "X.Y.Z"`. Must equal
      the tag (without the `v` prefix). CI fails fast if this drifts.
- [ ] `CHANGELOG.md` — add a `## [X.Y.Z] — YYYY-MM-DD` heading with the
      release notes for this tag. See "CHANGELOG" section below.

## 2. Update CHANGELOG.md

Format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

- [ ] Move everything currently under `## [Unreleased]` into a new
      `## [X.Y.Z] — YYYY-MM-DD` section above it.
- [ ] Leave `## [Unreleased]` in place with empty `Added / Changed / Fixed
      / Deprecated` subsections so the next cycle has a place to land.
- [ ] At the bottom of the file, append a comparison link:
      `[X.Y.Z]: https://github.com/NelCit/hlsl-clippy/compare/vX.Y.W...vX.Y.Z`

## 3. Local clean build + tests

- [ ] `cmake -B build --preset dev-release` (or `ci-msvc` on Windows)
- [ ] `cmake --build build` — Release config, no warnings.
- [ ] `ctest --test-dir build --output-on-failure` — all green.
- [ ] `./build/cli/hlsl-clippy --version` prints the new version string.
- [ ] `./build/lsp/hlsl-clippy-lsp --version` (or `--help` if no version
      flag yet) starts cleanly.

## 4. Commit + push the bump

- [ ] `git add core/src/version.cpp vscode-extension/package.json CHANGELOG.md`
- [ ] `git commit -s -m "release: vX.Y.Z"`
- [ ] `git push origin main`

## 5. Tag + push

- [ ] `git tag -a vX.Y.Z -m "Release vX.Y.Z"`
- [ ] `git push origin vX.Y.Z`

The tag push triggers both release workflows simultaneously.

## 6. Verify GitHub Release

After ~10–20 minutes the workflows finish. Open the release page at
`https://github.com/NelCit/hlsl-clippy/releases/tag/vX.Y.Z` and verify:

- [ ] **3 binary archives** attached:
  - `hlsl-clippy-X.Y.Z-windows-x86_64.zip`
  - `hlsl-clippy-X.Y.Z-linux-x86_64.tar.gz`
  - `hlsl-clippy-X.Y.Z-macos-aarch64.tar.gz`
- [ ] **3 SHA-256 sum files** alongside each archive (`.sha256`).
- [ ] **1 `.vsix` artifact**: `hlsl-clippy-X.Y.Z.vsix`.
- [ ] At least one binary downloads, extracts, and runs `--version`
      successfully on the appropriate platform.

## 7. Verify Marketplace listing (only if VSCE_PAT was set)

- [ ] Open `https://marketplace.visualstudio.com/items?itemName=nelcit.hlsl-clippy`
      — listing shows version `X.Y.Z`.
- [ ] `code --install-extension nelcit.hlsl-clippy` from a clean VS Code
      profile installs successfully.
- [ ] Open a `.hlsl` file in the test profile; the extension activates,
      downloads the matching `hlsl-clippy-lsp` binary, and lints.

If `VSCE_PAT` was *not* set (e.g. on a release cut from a fork), the
Marketplace step is skipped; users sideload the `.vsix` from the GitHub
Release.

## 8. Optional signing pre-work (if not done yet — see `.github/workflows/release.yml`)

Both signing tracks are optional in v0.5; the workflows ship unsigned
binaries gracefully when the secrets are absent. Tracked as v0.6 hardening
in ADR 0014 §"Risks & mitigations".

- [ ] **macOS notarization**: enrol in Apple Developer Program; create
      Developer ID Application certificate + App Store Connect API key;
      populate the `APPLE_NOTARY_KEY`, `APPLE_NOTARY_KEY_ID`,
      `APPLE_NOTARY_ISSUER_ID`, `APPLE_DEVELOPER_ID_CERT`, and
      `APPLE_DEVELOPER_ID_CERT_PASSWORD` secrets.
- [ ] **Windows code signing**: acquire EV (or OV) code-signing
      certificate; export `.pfx`; populate `WINDOWS_CERT` (base64) and
      `WINDOWS_CERT_PASSWORD` secrets.

## 9. Post-release housekeeping

- [ ] Update the `## [Unreleased]` section back to empty subsections if it
      somehow accumulated entries during the release flow.
- [ ] Announce on the project's communication channels (README badge
      auto-updates from the latest GitHub Release).
- [ ] If anything in the release pipeline misbehaved, capture it in a
      follow-up issue tagged `release-tooling` so the next cycle is
      smoother.
