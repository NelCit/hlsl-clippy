# Handoff Notes — Machine Stop at 2026-04-30

Eight background agents were stopped mid-flight. This document captures what was preserved and what needs resumption.

---

## 1. Status as of Stop Time

### On `origin/main` (after this handoff push):
- Phase 0 + Phase 1 rules: fully landed (base from initial scaffold)
- Phase 2 interp/trig/identity pack: **6 rules, fully merged** (commit `198079e`)
- Phase 3 bindings docs: **14 pages, fully merged** (commit `d351bce`)
- Phase 3 texture/sampling/workgroup docs: **11 pages merged** (3 of 4 batches; interpolators batch ~3 pages still pending on branch)
- Phase 4 control-flow docs: **10 pages merged** (2 of 5 batches; note: the "2 batches" includes the WIP auto-save commit)
- Phase 7 precision/memory docs: **11 pages merged** (2 of 6 batches)

### Local-only (NOT pushed — branches stay on this machine's disk):
- `worktree-agent-a4defe1caaa2384ed` — Phase 2 bit+redundant pack (6 rules, PARTIAL — WIP auto-save commit made)
- `worktree-agent-ac88026c8baf67d00` — Phase 2 misc pack (3 rules, PARTIAL — WIP auto-save commit made)

### Lost / Worktree Missing:
- `worktree-agent-addf9c16171bdd19c` (merge agent) — worktree directory not found; merge result already on main (`198079e`)
- `worktree-agent-ad92c0edf32c10e3d` (Phase 2 pow+vec pack, 8 rules) — worktree not found; work is **lost**, needs full re-dispatch

---

## 2. Branches Still on Disk (Not Merged)

| Branch / Agent ID | Worktree Path | State |
|---|---|---|
| `worktree-agent-a4defe1caaa2384ed` | `.claude/worktrees/agent-a4defe1caaa2384ed` | PARTIAL — 2 commits (3 rules done: redundant-normalize/transpose/abs; 3 rules WIP: countbits/firstbit/manual-mad). Registry wiring incomplete — do NOT merge without build verification. |
| `worktree-agent-ac88026c8baf67d00` | `.claude/worktrees/agent-ac88026c8baf67d00` | PARTIAL — 1 WIP commit (3 rules: compare-equal-float, comparison-with-nan-literal, redundant-precision-cast). Was struggling with cross-rule test-fixture brittleness — needs test fixes before merge. |
| `worktree-agent-aeee493b0e499c075` | `.claude/worktrees/agent-aeee493b0e499c075` | DONE — content already on main via merge `198079e`. Branch kept for reference; safe to delete. |

---

## 3. How to Resume on Another Machine

```bash
git clone https://github.com/NelCit/hlsl-clippy.git
cd hlsl-clippy
git submodule update --init --recursive
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
```

**Important:** The unmerged branches (`a4defe1`, `ac88026`) are **not pushed** — they exist only as local worktrees on the stopped machine. To resume them on another machine, you must re-dispatch via Claude Code using the prompts in `left_works/per-branch/<id>.md`.

To access the stopped machine's worktrees (if available over network):
```
c:/Users/vinle/Documents/hlsl-clippy/.claude/worktrees/agent-a4defe1caaa2384ed
c:/Users/vinle/Documents/hlsl-clippy/.claude/worktrees/agent-ac88026c8baf67d00
```

---

## 4. Phase 2 Rule Implementation Status

Phase 2 target: **24 rules total** across 4 packs.

| Pack | Rules | Branch | Status |
|---|---|---|---|
| interp/trig/identity (6 rules) | lerp-extremes, mul-identity, sin-cos-pair, manual-reflect, manual-step, manual-smoothstep | `aeee493` → merged | **DONE** on main |
| pow+vec/length (8 rules) | (8 rules — exact names TBD) | `ad92c0e` → MISSING | **LOST** — worktree gone, needs full re-dispatch |
| bit+redundant (6 rules) | redundant-normalize, redundant-transpose, redundant-abs, countbits-vs-manual-popcount, firstbit-vs-log2-trick, manual-mad-decomposition | `a4defe1` — local only | **PARTIAL** — 3 rules committed; 3 more WIP (untested). Registry wiring for all 6 committed but needs build check. |
| misc (3 rules) | compare-equal-float, comparison-with-nan-literal, redundant-precision-cast | `ac88026` — local only | **PARTIAL** — all 3 rules WIP; test fixture issues reported |

**On main: 6 / 24 Phase 2 rules** (the interp/trig pack).
**Partial/local: up to 9 more** (if bit+redundant and misc pass build checks).
**Lost: 8** (pow+vec pack).

---

## 5. Doc Page Status

| Phase | Pages on Main | Notes |
|---|---|---|
| Phase 0 (scaffold) | varies | Initial commit, not doc-heavy |
| Phase 1 | — | No docs tracked separately |
| Phase 2 interp/trig | 6 rule impls | Code rules, not doc pages |
| Phase 3 bindings | **14 pages** | Fully merged (`d351bce`) |
| Phase 3 texture/sampling/workgroup | **11 pages** | 3 of 4 batches merged (`e578165`); ~3 interpolator pages pending on stopped machine |
| Phase 4 control-flow | **10 pages** | 2 of 5 batches merged (`f6cf944`); ~15 pages pending |
| Phase 7 precision/memory | **11 pages** | 2 of 6 batches merged (`f190c21`); ~21 pages pending |

**Total doc pages on main: ~46 pages** (Phase 3 bindings 14 + Phase 3 texture 11 + Phase 4 10 + Phase 7 11).
**Pending (on stopped machine or lost): ~39+ pages.**

---

## 6. Open Issues at Stop Time

1. **Em-dash in test names** (`worktree-agent-ad92c0edf32c10e3d` / pow+vec pack): Agent was mid-fix on test name formatting with em-dash characters. This worktree is now MISSING. If re-dispatching, explicitly tell the agent to use ASCII hyphens only in `TEST(SuiteName, test_name)` identifiers — CTest chokes on Unicode characters in test names.

2. **Registry reconstruction concern** (`worktree-agent-a4defe1caaa2384ed`): Agent was "reconstructing CMakeLists/registry/rules.hpp/tests" — this suggests it may have overwritten or partially patched shared files. The WIP commit captured the state, but before merging this branch, run a full `cmake --build build && ctest --test-dir build` to verify nothing is broken. Do NOT merge this branch blindly.

3. **Cross-rule test fixture brittleness** (`worktree-agent-ac88026c8baf67d00`): The misc pack agent hit issues where adding new lint rules caused previously-passing tests from other rules to spuriously fail (the checker was being invoked globally). This is a test-isolation design problem. Before merging, ensure each test file only registers the rule it's testing, or that the test harness scopes rule registration per test suite.

4. **pow+vec pack fully lost**: `worktree-agent-ad92c0edf32c10e3d` worktree is gone. The 8 rules it was implementing (pow-of-two simplification, vector length patterns, etc.) need to be re-implemented from scratch. Check CLAUDE.md or any Phase 2 planning docs for the original rule list.

5. **Phase 3 interpolators batch missing**: `worktree-agent-aa8c1a334446dd59e` had batch 4 (interpolator rule pages, ~3 pages) in-progress when stopped. The 3 committed batches were merged. The interpolators batch needs resumption — check the agent's original prompt for the exact page list.

6. **Phase 4 and Phase 7 significantly incomplete**: Only 2 of 5 and 2 of 6 batches respectively are on main. These are doc-only and low-risk to re-dispatch.
