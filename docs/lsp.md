---
title: LSP / IDE
outline: deep
---

# LSP / IDE

`hlsl-clippy` ships a JSON-RPC LSP server (`hlsl-clippy-lsp`) that
thin-wraps the same diagnostic + fix engine the CLI uses. Any
LSP-speaking editor can drive it.

## VS Code extension

Recommended path on VS Code 1.85+. Search for **HLSL Clippy** by
`nelcit` in the Extensions view, or:

```sh
code --install-extension nelcit.hlsl-clippy
```

The Marketplace serves the matching per-platform `.vsix` for your
OS+arch (linux-x64 / win32-x64 / darwin-arm64). The LSP binary is
bundled inside the `.vsix` — no extra download required.

### Sideload from a `.vsix`

Per-platform `.vsix` files are also attached to every
[GitHub Release](https://github.com/NelCit/hlsl-clippy/releases):

- `hlsl-clippy-<version>-linux-x64.vsix`
- `hlsl-clippy-<version>-win32-x64.vsix`
- `hlsl-clippy-<version>-darwin-arm64.vsix`

```sh
code --install-extension hlsl-clippy-<version>-<target>.vsix
```

### Extension settings

| Setting | Type | Default | Purpose |
|---|---|---|---|
| `hlslClippy.serverPath` | `string` | `""` | Override the bundled LSP binary with a custom build. Empty = use the bundled binary (or `hlsl-clippy-lsp` on `PATH`). |
| `hlslClippy.targetProfile` | `string` | `""` | Slang target profile (e.g. `sm_6_6`, `vs_6_7`, `ps_6_8`). Empty = server default per shader stage. |
| `hlslClippy.enableReflection` | `boolean` | `true` | Enable Phase 3 reflection-aware rules. Disable on slow machines for AST-only latency. |
| `hlslClippy.enableControlFlow` | `boolean` | `true` | Enable Phase 4 control-flow / uniformity rules. |
| `hlslClippy.trace.server` | `enum` | `"off"` | LSP wire trace (`"off"`, `"messages"`, `"verbose"`) for debugging. |

## Other LSP clients

The LSP wire is generic. Any editor with LSP support can drive
`hlsl-clippy-lsp` directly. Get the binary from a tagged
[GitHub Release](https://github.com/NelCit/hlsl-clippy/releases) and
put it on `PATH`, then point your editor's HLSL filetype at it.

### Neovim (lspconfig)

```lua
require('lspconfig.configs').hlsl_clippy = {
  default_config = {
    cmd = { 'hlsl-clippy-lsp' },
    filetypes = { 'hlsl' },
    root_dir = require('lspconfig.util').root_pattern('.hlsl-clippy.toml', '.git'),
  },
}
require('lspconfig').hlsl_clippy.setup{}
```

### Helix

In `~/.config/helix/languages.toml`:

```toml
[language-server.hlsl-clippy]
command = "hlsl-clippy-lsp"

[[language]]
name = "hlsl"
language-servers = ["hlsl-clippy"]
```

### Emacs lsp-mode

```elisp
(with-eval-after-load 'lsp-mode
  (lsp-register-client
   (make-lsp-client :new-connection (lsp-stdio-connection "hlsl-clippy-lsp")
                    :major-modes '(hlsl-mode)
                    :server-id 'hlsl-clippy)))
```

## Workspace awareness

The LSP server reads `.hlsl-clippy.toml` per-document using the same
walk-up logic as the CLI (see [Configuration](/configuration#walk-up-resolution)).
Multi-root workspaces are supported — each root resolves its own
config independently.

A file watcher on `.hlsl-clippy.toml` invalidates the cached `Config`
for every open document under the changed config root, so editing
the config takes effect immediately without an editor restart.

## Quick-fixes

Rules with machine-applicable fixes surface as VS Code code actions
under the lightbulb. The action title shows the actual replacement
(e.g. *"Replace pow(x, 2.0) with x * x"*) so the rewrite intent is
visible before you click. Other LSP clients expose the same actions
via `textDocument/codeAction` requests.

## Latency budget

Per ADR 0014 §3, the LSP server targets:
- **Incremental re-lint of a saved buffer:** &lt;100 ms p50.
- **Cold reflection** (first compile of a source via Slang):
  may exceed 500 ms; surfaced as `linting…` status.
- **AST-only rule packs** (Phase 0/1/2): hit the budget cold,
  because the reflection engine never constructs for those packs.

If your typical edit-cycle latency is too high, disable reflection
or control-flow stages via the settings above.
