---
description: Branch, commit, and open a PR for completed kaadanTD phase work. Owner-invoked only.
argument-hint: "[phase-number]"
arguments: [phase]
disable-model-invocation: true
allowed-tools: Read, Bash, Grep, Glob
---

# Ship phase $phase

This skill exists so that committing is always a deliberate act by the owner, never something
decided mid-task. It is invoked by the owner and never automatically.

Current state:

!`git status -sb && echo "--- diff summary ---" && git diff --stat HEAD && echo "--- untracked ---" && git ls-files --others --exclude-standard`

## Steps

**1. Verify before committing, not after.** Run `ctest --preset core` and paste the real
result. If anything fails, stop and report — do not commit red.

**2. Review your own diff.** Read `git diff HEAD` in full. Check for debug prints, commented
scaffolding, unrelated drive-by edits, and anything that belongs to a different phase. Removing
it now is cheaper than the owner catching it in review.

**3. Confirm the invariants held.** `ctest --preset core -R purity` must pass. If this phase
touched `game/`, confirm the determinism guard specifically.

**4. Branch.** Never commit to `main`. Use `phase-0$phase/<short-topic>` or
`build/<topic>` for infrastructure. If the phase depends on unmerged work, branch from that
branch rather than `main`, and say so in the PR body.

**5. Commit.** The message explains *why*, not just what. State what changed, what was
deliberately left out, and any risk carried forward. End with:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
```

**6. Push and open the PR** with `gh pr create`. The body should give the owner what they need
to review in ten minutes: what this phase was for, the acceptance criteria and whether each is
met, the test output, what was deliberately skipped, and anything unproven. End the body with:

```
🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

**7. Report the PR URL** and stop. Do not merge — that is the owner's gate, and it is the only
one.

## Honesty requirement

If part of the phase is incomplete, the PR says so in the body, prominently. A PR that claims
a phase is done when it is partial defeats the entire review gate, which is the one mechanism
standing between a wrong turn and two weeks of compounding on it.
