---
title: Troubleshooting / FAQ
outline: deep
---

# Troubleshooting / FAQ

## Install / build

### `cmake -B build` fails with "could not locate Slang"

You missed the Slang-prebuilt-cache bootstrap step. From the repo root:

```sh
bash  tools/fetch-slang.sh         # Linux / macOS
pwsh  tools/fetch-slang.ps1        # Windows
```

`cmake/UseSlang.cmake` looks for the prebuilt at
`$HOME/.cache/shader-clippy/slang/<version>/` (POSIX) or
`%LOCALAPPDATA%\shader-clippy\slang\<version>\` (Windows). Power users can
also export `Slang_ROOT=/path/to/your/slang/install` to point at a
custom build. See [`external/slang-version.md`](https://github.com/NelCit/shader-clippy/blob/main/external/slang-version.md)
for details.

### macOS: "no template named 'expected' in namespace 'std'"

Apple's bundled Clang is too old for C++23. Install Homebrew's `llvm@18`
and prepend its bin/ to PATH:

```sh
brew install cmake ninja llvm@18
export PATH="/opt/homebrew/opt/llvm@18/bin:$PATH"
```

Or just `. tools/dev-shell.sh` — it does this automatically (idempotent).

### Linux: `std::expected` symbol-visibility errors with `clang++-18`

Ubuntu 24.04's `libstdc++ 13` ships `<expected>` but with reliability gaps.
Use Clang's bundled `libc++` instead:

```sh
sudo apt-get install -y libc++-18-dev libc++abi-18-dev
export CXXFLAGS="-stdlib=libc++"
export LDFLAGS="-stdlib=libc++"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Or use the `ci-clang` preset:

```sh
cmake --preset ci-clang
```

### Windows: `slang.dll not found` when running tests

Dot-source `tools\dev-shell.ps1` at the start of your PowerShell session
— it puts the Slang prebuilt-cache `bin/` on PATH so the test runner
can resolve the runtime DLLs.

```powershell
. .\tools\dev-shell.ps1
ctest --test-dir build --output-on-failure
```

The bundled-PATH only lives for the current shell; new shells need to
dot-source again (idempotent).

### `ctest` reports 4 failures on a fresh `main` build

Expected. Four pre-existing `STATUS_STACK_BUFFER_OVERRUN` crashes in
`test_golden_snapshots.cpp` are tracked in
[tests/KNOWN_FAILURES.md](https://github.com/NelCit/shader-clippy/blob/main/tests/KNOWN_FAILURES.md).
The other 667 tests should all pass.

### CMake preset `dev-debug` / `ci-msvc` "No such preset"

Bump CMake to 3.25+ (presets v6 schema). On Linux:

```sh
sudo apt-get install -y cmake
cmake --version    # need >= 3.25
```

## Linting / config

### Rule X is firing where I don't want it

Three escalation levels:

1. **Per-line / per-block** — drop a comment:
   ```hlsl
   float k = pow(x, 2.0);  // shader-clippy: allow(pow-const-squared)
   ```
2. **Per-directory** — add an `[[overrides]]` block to your
   `.shader-clippy.toml` ([reference](/configuration#per-directory-rule-tuning)).
3. **Globally** — set the rule to `"off"` in `[rules]`:
   ```toml
   [rules]
   pow-const-squared = "off"
   ```

### Rule X is *not* firing where I expect it

- Check your config: a `[[overrides]]` higher in the directory tree may
  be silencing it. Run with `--config /dev/null` to bypass walk-up
  resolution and confirm the rule fires when configured fresh.
- Reflection-aware rules (Phase 3) need Slang to compile the shader
  successfully. If your shader has a syntax error or missing entry
  point, reflection-stage rules silently skip. Look for a
  `clippy::reflection` warning at the top of the output.
- Control-flow rules (Phase 4) depend on the CFG being built. Check
  for a `clippy::cfg` warning if the CFG couldn't construct.

### `cbuffer X : register(b0) { ... }` parses to an ERROR node

Known limitation in the upstream tree-sitter-hlsl grammar (v0.2.0).
The explicit register-binding suffix on `cbuffer` declarations isn't
parsed. Fallback: rules that need that information go through Slang
reflection instead of the AST.

Tracked upstream in `external/treesitter-version.md`. Patches to the
grammar are welcome.

### `--fix` silently does nothing

A few possible causes:

- The diagnostic doesn't have a machine-applicable fix. Check the
  `applicability` field on the rule's doc page — `none` and
  `suggestion` rules don't auto-fix.
- Two rules' fixes overlap and the `Rewriter` dropped the lower-priority
  one. Look for a `clippy::fix-conflict` note in the output.
- The file is read-only or owned by another user.

### `--fix` produces broken HLSL

File a [bug report](https://github.com/NelCit/shader-clippy/issues/new?template=bug_report.yml)
with the input shader, the rule id that fired, and the broken output.
Machine-applicable fixes should be type-safe; if one isn't, that's a
correctness bug.

## VS Code extension

### Extension installed but no diagnostics show

1. Open the Output panel (`View → Output`) and pick **Shader Clippy** from
   the dropdown. Look for an error from the LSP server lookup.
2. The most common cause behind a corporate firewall is that the
   bundled binary path didn't resolve — ensure you installed the
   per-platform `.vsix` matching your OS+arch (Marketplace handles this
   automatically; if you sideloaded, double-check the filename).
3. Set `shaderClippy.serverPath` in VS Code settings to point at a
   manually-installed `shader-clippy-lsp` binary as a fallback.

### Hover shows "untrusted workspace" warning

VS Code marks workspaces as untrusted by default. The Shader Clippy
extension is a normal extension that doesn't need elevated trust.
"Trust this workspace" once and the LSP starts.

### Extension is slow on a large mod

Disable Phase 4 control-flow rules:

```jsonc
{
  "shaderClippy.enableControlFlow": false
}
```

Or Phase 3 reflection rules too:

```jsonc
{
  "shaderClippy.enableReflection": false
}
```

The CLI's `--profile` flag for per-rule timing is on the v0.6 backlog.

## CI integration

### `--format=github-annotations` shows annotations but they're not on the PR diff

GitHub renders annotations on PR diffs only when the workflow run
itself is associated with the PR. Make sure your workflow triggers on
`pull_request:` (not just `push:`).

### Exit code `2` — was it errors or a config problem?

Today exit code `2` is overloaded — both rule errors and invocation
failures use it. Check stderr for `clippy::config` / `clippy::reflection`
diagnostic codes to disambiguate. A dedicated config-error exit code
is on the v0.6 backlog.

## Filing a useful bug report

Include:

- Output of `shader-clippy --version`
- Your OS + version (`uname -a` / `winver`)
- The shader that triggered the issue (simplify if possible)
- Your `.shader-clippy.toml` (or note "no config")
- The full `--format=json` output (it carries byte offsets that help
  reproduce the issue)
- For LSP issues: contents of the **Shader Clippy** output channel

The [bug-report template](https://github.com/NelCit/shader-clippy/issues/new?template=bug_report.yml)
prompts for these fields.
