# 0010 — The state hash covers the content database

**Status:** accepted · 2026-08-24

## Context

Phase 3 moved every balance number out of `Config.h` and into content files. Before that, the
numbers were compiled in, so "same binary" implied "same balance" and the state hash did not
have to say anything about them.

That implication is gone. Two runs can now share a seed, a command log and a binary, and still
be different runs because someone edited `towers.ktd` in between. Hot reload does this
deliberately, several times a minute.

## Decision

**`ContentDb::fingerprint()` — a 64-bit hash over the database's own canonical serialization —
is the first field `World::serialize()` writes.**

Display names are excluded. Everything else a def carries is included.

## Why

ADR 0008's rule is that a field missing from the serializer is a divergence the hash cannot
see. The balance numbers are now exactly such a field: they left the binary but did not stop
determining the run.

Without this, a replay recorded against one set of numbers and played back against another
would report matching checksums right up to the tick the difference first mattered, and then
diverge — with the hash pointing at whichever entity happened to notice first rather than at
the actual cause. With it, the very first tick disagrees and the reason is unambiguous.

Names are excluded because they are the one part of a def that cannot change the simulation.
Localising the game must not invalidate every recorded replay, and a translator should not be
able to break the determinism test.

The fingerprint is computed once in the `World` constructor and cached. The database cannot
change under a running world — a hot reload builds a new one and a new world with it — so
recomputing it per tick would be pure cost on the path the CI trace exercises 3,000 times.

## Consequences

- **A balance edit changes the golden checksum**, which means `determinism_golden` fails and
  `tests/data/reference_trace.tsv` has to be regenerated. That is the intended workflow, not
  friction to route around: a balance change *is* a change to what every replay means, and
  making it visible in the same commit is the point.
- It fails loudly for content changes that could not have affected the scripted run — adding a
  third tower type nobody builds, say. Accepted deliberately: "this replay was recorded against
  different data" is the question worth answering, and answering it conservatively is cheap
  while answering it precisely would mean tracking which defs a run actually touched.
- Phase 7's save files get the same protection for free, since the save writer is the same
  serializer with a different sink.
