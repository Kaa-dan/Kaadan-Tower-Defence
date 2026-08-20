---
description: Start work on a kaadanTD roadmap phase. Loads that phase's spec and executes it against its acceptance criteria. Use when asked to start, continue, or work on a phase — e.g. "start phase 1", "do the next phase", "let's do determinism".
argument-hint: "[phase-number]"
arguments: [phase]
allowed-tools: Read, Grep, Glob, Bash, Edit, Write, Agent
---

# Work roadmap phase $phase

If `$phase` is empty, read the ledger and pick the first phase not marked done.

Ledger:

@docs/roadmap/README.md

## How to run a phase

**1. Load the spec.** Read `docs/roadmap/phase-0$phase-*.md`. It carries the goal, the
constraints, the acceptance criteria, and what is explicitly out of scope. Read the whole
thing before touching code.

**2. Confirm the starting state is green.** Run `ctest --preset core`. If it is already
failing, stop and say so — do not build on a broken base.

**3. Check the decisions.** Skim `docs/decisions/` for anything bearing on this phase. If you
find yourself about to re-litigate a recorded decision, don't; the reasoning is in the ADR. If
you think an ADR is genuinely wrong, say so explicitly and let the owner decide, rather than
quietly diverging.

**4. Explore before editing.** Delegate broad reading to Explore subagents so their file dumps
stay out of this context. Read directly only the handful of files you will actually modify.

**5. Implement against the acceptance criteria.** The criteria name tests. Write the tests.
They should fail first — a test that passes before you have written the implementation is
testing nothing. Then make them pass.

**6. Verify honestly.** Run `ctest --preset core` and, when the phase touches simulation
internals, the `asan` preset too. Paste real output. If something fails or you skipped part of
the scope, say so plainly — a phase reported done that isn't is worse than a phase left open.

**7. Record what you learned.** Append a `docs/JOURNAL.md` entry (newest first) and write ADRs
for any non-obvious call you made. Do this *before* context runs low.

**8. Update the ledger** in `docs/roadmap/README.md` to reflect real state.

**9. Stop.** Do not commit, push, or open a PR. The owner runs `/ship-phase` when they are
ready. Summarize what changed, what the tests say, and anything you deliberately left out.

## Scope discipline

Do the phase that was asked for. If you spot something broken in another phase's territory,
note it in the ledger's known-bugs list and move on — do not fix it here. A PR that quietly
spans three phases cannot be reviewed, and review is the owner's only gate.
