# Journal

Append-only, newest first. One entry per working session. Written *before* context runs low,
not after.

Keep entries short and factual: what changed, what surprised us, what's next. Reasoning that
will matter in six months belongs in an ADR, not here.

---

## 2026-08-20 — Phase 1: determinism hardening

Ten commits on `phase-01/determinism`, branched from `main` after the harness PR merged.

**The shape of it:** an audited `game/Math.h` (the only file in `game/` that may include
`<cmath>`, now enforced), IEEE float semantics pinned at the build level with a fast-math
deny check, `std::pow` replaced by `math::powi` computed once per wave, `Rng` extracted with
splitmix64 seed mixing and Lemire ranges and per-consumer streams, `step()` converted from a
`dt` parameter to an integer tick clock, a canonical world serialization with a 64-bit
checksum, `ktd_hashdump` plus a committed 3,000-tick reference trace, a six-way CI trace
comparison, and `-Wconversion`/`-Wdouble-promotion` switched on.

All eight acceptance criteria from the spec are implemented. 44 tests pass under `core`,
`core-release` and `asan`; the full SDL app builds; clang-format is clean.

**What this does not prove.** The criterion that matters — `determinism_cross_platform` — can
only pass on CI, and CI has still never run on this repo. Everything here is green on macOS
only. That is now the project's blocking open item rather than a footnote.

**Three things worth remembering:**

*The warnings pass paid for itself immediately.* `-Wdouble-promotion` found
`while (accumulator >= config::kFixedDt)` in `app/main.cpp` — a `double` compared against a
`float`, promoting implicitly, inside the loop that decides how many simulation steps a frame
gets. That is exactly the float/double mixing the phase spec said must not happen, and it was
sitting in the one loop where it matters. Found by the compiler, not by reading. Total fallout
from both warnings was 32 diagnostics, far less than the "wave of noise" the Phase 0 comment
predicted.

*A test that cannot fail is worse than no test.* The first draft of `rng_rangeint_is_unbiased`
was a chi-squared check over six buckets. It would have passed against the old biased
implementation, because modulo bias for a span of 6 is about one part in a billion. Rewrote it
with a span of 3 * 2^30 where the bias is 50% versus 33%, then verified it discriminates by
temporarily reverting `bounded()` to the modulo form and confirming that test — and only that
test — failed. Did the same for the reference-trace guard by corrupting tick 1847 and checking
it named tick 1847.

*Not every design number survives a tick conversion.* `kTowerShotsPerSecond = 1.6` is 37.5
ticks. It became `kTowerReloadTicks = 38`, a real rate of 1.579/second, so towers are ~1.3%
slower. Every test still passed, which means nothing depended on the old value — but this is a
design number changed by an engineering constraint and it needs the owner's eye, not silent
inheritance. Flagged in the commit and in the PR-facing notes.

**Bug found and left alone:** `waveInProgress()` stays true forever once the game reaches
`Defeat`, because `step()` returns early and the field never clears. Harmless in the game,
but it hangs any loop that waits on it — which it did, to me, while writing a test. Logged as
known-wrong #8 rather than fixed inside this phase.

**Deliberately not done:** `sinTurns`/`cosTurns` and `atan2Approx` in `Math.h`. Nothing needs
them yet, and a table-driven trig implementation with no caller is a guess about requirements.

**Next:** Phase 2, entity storage. The state hash exists now, which is what makes that refactor
checkable — the golden constant `0x6684E7DA901C0081` must survive it unchanged, and if it does
not, the storage change altered behaviour.

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
