# Branch: worktree-agent-ad92c0edf32c10e3d

**Branch name:** `worktree-agent-ad92c0edf32c10e3d`
**Worktree path:** `c:/Users/vinle/Documents/hlsl-clippy/.claude/worktrees/agent-ad92c0edf32c10e3d` (**MISSING — worktree directory not found**)
**Status at stop:** LOST — worktree directory does not exist on disk

---

## Job Summary

Phase 2 implementation — **pow + vec/length rule pack (8 rules)**:
- Exact rule names were in the agent's original prompt (not captured here)
- Likely included: pow-of-two simplifications, vector length/distance patterns
- 8 rules total

---

## Commits on Branch (above 0eaf912)

```
(none — worktree not found, branch state unknown)
```

The worktree at `.claude/worktrees/agent-ad92c0edf32c10e3d` was not present when this handoff was created. Either the worktree was never created, was cleaned up, or the directory was removed.

---

## Working-Tree Status When Stopped

```
(not available — worktree missing)
```

---

## Last-Known Agent Output

> Was fixing em-dash test-name issue; partial commits possible

The agent was mid-fix on a test name formatting problem. CTest does not support Unicode characters in test names, and the agent had used em-dashes (`—`) in `TEST(Suite, name)` identifiers, which caused CTest registration failures. The agent was replacing these with ASCII hyphens when it was stopped.

No worktree exists to recover from. All work is lost.

---

## How to Resume (Full Re-Dispatch Required)

Since the worktree is gone, this needs a completely fresh implementation.

### Re-dispatch prompt

Dispatch a new Claude Code agent to implement the pow+vec/length pack from scratch:

```
Implement Phase 2 hlsl-clippy lint rules — pow + vec/length pack (8 rules).

These rules go in core/src/rules/ (one .cpp per rule), declared in core/src/rules/rules.hpp,
registered in core/src/rules/registry.cpp, built via core/CMakeLists.txt,
and tested in tests/unit/ (one test .cpp per rule).

Rules to implement (confirm with CLAUDE.md or roadmap for exact names):
- pow-of-two-as-shift: detect pow(x, 2.0) patterns that could be x*x or x<<1 for int
- pow-one: detect pow(x, 1.0) — always x
- pow-zero: detect pow(x, 0.0) — always 1.0
- length-squared-compare: detect length(v) > k patterns that should use dot(v,v) > k*k
- normalize-then-dot: detect normalize(v) patterns where dot(v,v) > 0 check suffices
- distance-vs-length-sub: detect length(a-b) that could use distance(a,b)
- (any 2 additional pow/vec patterns from the Phase 2 roadmap)

Follow exactly the same file structure and CMake pattern as the interp/trig rules (commit 198079e on main).

CRITICAL: Use only ASCII characters in TEST() identifiers. Use underscores, not hyphens or em-dashes.
Example: TEST(PowRules, pow_of_two_as_shift_simple) — NOT TEST(PowRules, pow-of-two-as-shift).

Target: /W4 /WX clean build + 99/99 ctest pass.
```

**Note:** Verify the exact rule list against `CLAUDE.md` or any Phase 2 planning document in the repo before dispatching.
