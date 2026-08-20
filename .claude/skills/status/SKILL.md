---
description: Report where the kaadanTD project stands — current phase, last session, git state, and whether the build is green. Use when asked "where are we", "what's next", "what's pending", or at the start of a session to orient.
allowed-tools: Read, Bash, Grep, Glob
---

# Project status

Current git state:

!`git log --oneline -5 && echo "--- branch ---" && git status -sb | head -3 && echo "--- unmerged branches ---" && git branch -a --no-merged main 2>/dev/null | head`

Roadmap ledger:

@docs/roadmap/README.md

Most recent journal entry:

!`sed -n '1,40p' docs/JOURNAL.md 2>/dev/null || echo "(no journal yet)"`

## What to report

Give the owner a short, scannable answer covering:

1. **Current phase** — which one is next per the ledger, and whether anything is mid-flight
   (uncommitted changes, an open branch that isn't merged).
2. **Last session** — one line from the journal head.
3. **Build health** — if it matters to the question, run `ctest --preset core` and report the
   real result. Do not assert green without running it.
4. **Blockers and open decisions** — anything in the ledger's loose-ends section that is
   waiting on the owner rather than on you.

Keep it under about fifteen lines unless asked to expand. Lead with what is next, not with a
recap of what is done.
