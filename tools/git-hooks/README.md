# Git hooks

Source of truth for project-managed git hooks. The installer copies these
files into `.git/hooks/`.

## Install

```sh
# Windows
pwsh tools\install-hooks.ps1

# Linux / macOS
bash tools/install-hooks.sh
```

The installers are idempotent — re-run any time after pulling new hook
versions.

## Hooks

### `pre-commit`

Runs `clang-format --dry-run --Werror` against the staged C++ files under
`cli/`, `core/`, `tools/`, `lsp/` (matching `.cpp`, `.hpp`, `.h`;
`external/` is excluded). Mirrors the file glob enforced by
`.github/workflows/lint.yml`, so a clean local commit is a clean CI run.

If no C++ files are staged, the hook exits silently and the commit
proceeds.

If clang-format cannot be located, or the resolved binary's major version
is not 18 (the CI baseline), the hook refuses the commit.

## Override env vars

| Variable                                  | Purpose                                                         |
|-------------------------------------------|-----------------------------------------------------------------|
| `CLANG_FORMAT`                            | Explicit path to a `clang-format` binary (overrides PATH lookup) |
| `HLSL_CLIPPY_HOOK_ALLOW_ANY_CLANG_FORMAT` | Set to `1` to skip the major-version-18 check                   |
| `HLSL_CLIPPY_HOOK_FIX`                    | Set to `1` to auto-run `clang-format -i` and `git add` the staged files instead of rejecting the commit |

Example — rerun a rejected commit with auto-fix:

```sh
HLSL_CLIPPY_HOOK_FIX=1 git commit -s -m "feat: add foo rule"
```

## Notes

- The hook is POSIX `/bin/sh`. On Windows it runs under the shim shipped
  with Git for Windows.
- `.git/hooks/` is user-local and not tracked by git, which is why the
  hook ships under `tools/git-hooks/` and is copied in via the installer.
- To uninstall, delete `.git/hooks/pre-commit`.
