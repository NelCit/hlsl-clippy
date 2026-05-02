# HLSL Clippy VS Code extension — local development

Two ways to test the extension before tagging a release. Both produce
real, end-to-end behaviour: the LSP server is spawned, diagnostics
flow over JSON-RPC, code actions render, etc. Pick whichever matches
your iteration rhythm.

## A. Extension Development Host (fastest dev loop)

The "F5 dev loop": VS Code launches a second window with the
extension loaded from your working tree. Reload-on-recompile is
instant. Best for iterating on the TypeScript side.

1. Open the **`vscode-extension/`** directory (not the repo root) in
   VS Code:
   ```
   code vscode-extension
   ```
2. Hit **`F5`**. VS Code:
   - Runs `npm run compile` (via `.vscode/tasks.json`).
   - Spawns a child VS Code window titled "[Extension Development Host]".
   - Loads the extension from `./out/extension.js` of the dev tree.
3. In the child window, **open a `.hlsl` file** (this repo has plenty
   under `tests/fixtures/` and `tests/golden/fixtures/`).
4. Look for the **`$(check) HLSL Clippy`** badge in the bottom-right
   status bar. Squiggles + Problems entries should appear within a
   second.
5. Re-compile after a TypeScript edit: in the parent window, run
   **`Ctrl+Shift+B`** (default build task = `tsc -p .`), then in the
   Extension Development Host window run **`Developer: Reload Window`**
   from the command palette.

The Extension Development Host inherits the parent's environment, so
the `hlsl-clippy-lsp` binary lookup walks the same chain as a real
install:

1. `hlslClippy.serverPath` setting (point at `build/lsp/hlsl-clippy-lsp.exe`
   for fastest iteration when you change the C++ side too).
2. PATH lookup.
3. Bundled binary at `vscode-extension/server/<platform>/`.
4. Cached download.
5. GitHub Releases download.

Set `HLSL_CLIPPY_LSP_DEBUG=1` (already in `launch.json`) to make the
LSP log to stderr; you'll see it under the **HLSL Clippy** Output
channel in the child window.

## B. Build the real .vsix and install it (highest fidelity)

This mirrors what the Marketplace serves: a single packaged `.vsix`
that bundles the LSP binary + every Slang runtime DLL. Use this
whenever you want to validate the bundling itself (e.g. after
touching `release-vscode.yml` or `tools/build-vsix-local.*`).

### Windows

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-vsix-local.ps1
```

### Linux / macOS

```bash
bash tools/build-vsix-local.sh
```

Both scripts:

1. Build `hlsl_clippy_lsp` via cmake (uses the existing build tree).
2. Stage `hlsl-clippy-lsp[.exe]` + every sibling Slang DLL / `.so` /
   `.dylib` into `vscode-extension/server/<platform>/`.
3. Run `npx tsc -p .` against `vscode-extension/` (catches strict-TS
   errors like `noUnusedParameters` BEFORE they break a release).
4. Run `npx vsce package --target <vscode-target>`.
5. Run `code --install-extension <built>.vsix --force` so your real
   VS Code installs it on top of any Marketplace install.

After installation, **reload VS Code** (`Ctrl+Shift+P` → "Developer:
Reload Window"). Open a `.hlsl` file. The status badge should turn
green within ~2 seconds.

To uninstall the locally-built version and restore the Marketplace
copy:

```
code --uninstall-extension nelcit.hlsl-clippy
code --install-extension nelcit.hlsl-clippy
```

### Environment overrides

| Variable | Effect |
| --- | --- |
| `HLSL_CLIPPY_SKIP_BUILD=1` | Reuse the existing `build/` tree (no `cmake --build` step). |
| `HLSL_CLIPPY_SKIP_INSTALL=1` | Build the `.vsix` but don't install it (useful in CI dry-runs). |

## C. LSP smoke test (cheapest)

A pre-flight that proves the `hlsl-clippy-lsp.exe` binary actually
handshakes over JSON-RPC stdio and emits diagnostics. Catches both
classes of LSP startup failure (missing Slang DLLs, CRLF mangling on
Windows text-mode stdio). Takes ~3 seconds; runs without VS Code.

```sh
node tools/smoke-lsp.js
```

Expected output ends with:

```
PASS: 23 diagnostic(s) emitted across 1 frame(s).
```

If it ends with `FAIL: no diagnostics emitted.` the LSP is broken at
the binary level; do not bother packaging the .vsix until this passes.

## Pre-release checklist (cheap version)

Before tagging `vX.Y.Z`, run **at minimum**:

```sh
# repo root: 3-second binary-level proof the LSP actually handshakes
node tools/smoke-lsp.js

# then build + install the real .vsix
bash tools/build-vsix-local.sh   # or .ps1 on Windows
```

Then in the freshly-installed extension:

1. Open `tests/golden/fixtures/phase2-math.hlsl`. Confirm at least
   one squiggle appears under a `pow(...)` call.
2. Hover the squiggle. Confirm the message is readable + has a
   "Companion blog post" link in the hover content.
3. Click the lightbulb (`Ctrl+.`). Confirm at least three actions:
   the LSP-provided fix + `HLSL Clippy: suppress '<rule>' for this
   line` + `HLSL Clippy: open '<rule>' docs`.
4. Right-click in the editor. Confirm the six HLSL Clippy commands
   appear inline (no submenu nesting).
5. Click the status-bar badge. Confirm the quick-pick menu opens.

If any of those fail, **do not tag**. Iterate locally until they
all pass. The release pipeline will not catch user-visible UX
regressions; only you will.

## Known traps

- **`vscode.workspace.save()`** is VS Code 1.86+ API. Our
  `engines.vscode: ^1.85.0` allows 1.85; on 1.85 the function is
  `undefined` and calls throw `TypeError`. Use
  `vscode.commands.executeCommand("workbench.action.files.save")`
  instead -- stable since 1.0. (Caught in v0.6.3.)
- **`tsconfig.json` has `noUnusedParameters: true`.** Any
  `provideX(a, b, c)` where you don't read all three needs underscore-
  prefixed names (`_a`). The CI lint job runs `tsc -p .` to catch
  this on every PR. (Caught in v0.6.5.)
- **`vsce package --target X`** does not auto-bundle native binaries.
  The release workflow stages them via a separate step BEFORE
  packaging; `tools/build-vsix-local.*` does the same. Skipping the
  staging step produces a `.vsix` that crashes the LSP at runtime.
  (Caught in v0.6.1.)
