# Journal

Append-only, newest first. One entry per working session. Written *before* context runs low,
not after.

Keep entries short and factual: what changed, what surprised us, what's next. Reasoning that
will matter in six months belongs in an ADR, not here.

---

## 2026-08-20 — AI development harness

Set up the project so a cold session can pick up work without the owner re-explaining anything.

**Added:** `CLAUDE.md` (session-loaded, kept under 80 lines), `.claude/rules/` scoped to
`game/**` and `engine/**`, three skills (`/status`, `/phase`, `/ship-phase`), a
`determinism-auditor` subagent, a `PostToolUse` hook that runs the purity guards whenever a
`game/` file changes, and `docs/` — architecture, six ADRs, ten phase specs, this journal.

**Migrated:** the roadmap out of a scratch file outside the repo, and the three Claude-memory
notes into ADRs 0001, 0002, and 0006. Both were machine-local and would have died with the
laptop.

**Method chosen:** spec-driven with *executable* acceptance criteria. Every phase spec names
tests rather than describing intent in prose. The reasoning: prose specs rot and cannot be
checked, and on an AI-built project tests are the only ground truth available — I cannot
playtest. `core_purity` already proved the pattern works, having survived five commits without
erosion because it fails the build rather than asking politely.

**Surprise:** the `PostToolUse` hook was tested on a clean file, a non-`game/` file, and an
injected `#include <SDL3/SDL.h>`. Worth noting the pattern from Phase 0 held again — the guard
had to be tested by breaking something, not just by watching it pass.

**Deliberately not done:** `LICENSE` (owner's call), and no C++ changed — all 24 tests should
be bit-identical before and after.

**Next:** Phase 1, determinism hardening. Fixes known-wrong #1 (`std::pow` cross-platform
divergence) and #7 (RNG modulo bias), and lands the cross-OS hash test that turns determinism
from an aspiration into a checked property.

---

## 2026-08-20 — Phase 0, infrastructure

Shipped `340f0a7` on `build/phase-0-infrastructure`. Presets, CI, `-Werror`, SDL pinned to a
SHA, per-test CTest granularity (24 entries, was 2), purity guards split three ways.

**Biggest win:** `KTD_TESTS_ONLY` — 0.7s to configure without SDL vs 94.4s with it. That is
what makes a per-push 3-OS CI matrix affordable rather than something you disable out of
impatience.

**Surprise:** the new determinism guard's first run failed on `Types.h`, matching `rand(`
inside the doc comment that says *"Never use rand()"*. Fixed by stripping comments before token
matching. Then verified all three guards actually fire by injecting real violations — a guard
that never fails proves nothing.

**Carried risk:** the CI workflow has never executed. Green locally on macOS only; the Linux
and Windows legs are unproven.
