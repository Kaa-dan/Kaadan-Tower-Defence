# Journal

Append-only, newest first. One entry per working session. Written *before* context runs low,
not after.

Keep entries short and factual: what changed, what surprised us, what's next. Reasoning that
will matter in six months belongs in an ADR, not here.

---

## 2026-08-24 — Phase 3: balance is data, and F5 replays the run against it

`Config.h` is down to three declarations: the tick rate, `kFixedDt`, and `ticksFromSeconds`.
Everything else — tower range and damage, enemy health and growth, wave sizes, the economy,
the level's wall layout — lives in `content/*.ktd` and is read through `content::ContentDb`.
Two tower types ship, which was the thing that was structurally impossible a day ago.

**What went in.** A hand-written TOML subset (`game/content/Parser.h`), the database and its
two-pass loader (`game/content/Database.h`), the level and wave schemas, `Platform::readFile`
in the SDL backend, and F5 hot reload in `app/`. 78 tests, all green, and green under
ASan+UBSan too.

**The float parser earned its keep immediately.** `parse_float_is_deterministic` pins eighteen
decimal strings to exact `binary32` bit patterns *and* cross-checks each against the C++
literal of the same digits, so the table cannot drift from the language. The conversion is one
`uint64` mantissa, one IEEE multiply or divide by an exactly-representable power of ten, one
narrowing — and it **refuses** anything outside that envelope (more than 15 significant digits,
exponent past ±22) rather than rounding. A silently-rounded balance number was the failure ADR
0005 existed to prevent, and refusing is the only way to actually prevent it.

**Unknown keys are errors.** Not a design flourish: `dammage = 40` loading fine and leaving the
damage at its default is the single most common way a data-driven game ships wrong numbers, and
it is invisible precisely because nothing fails. So `Fields::rejectUnknownKeys()` runs on every
table and a typo costs a build instead of a playtest.

**The loader validates routability.** It builds a throwaway `Grid` and `FlowField` and refuses a
level with no route from spawn to goal. A missing `.` in a map row otherwise loads perfectly and
presents enemies standing still at the entrance, which is a mystifying way to discover a typo.

**The hash now covers the content** ([ADR 0010](decisions/0010-content-fingerprint-in-the-hash.md)).
This is the phase's one non-obvious call. Balance left the binary but did not stop determining
the run, so under ADR 0008's rule it belongs in the serializer. Display names are excluded —
localisation must not invalidate replays.

**Two surprises, both in tests rather than in the simulation.**

`-Wfloat-equal` is on, so the one place that genuinely needs exact float comparison — the whole
point of the deterministic parser — cannot use `==`. Comparing `std::bit_cast<uint32_t>` instead
is more honest anyway: it says "these bits" rather than "these values".

And `ctest` silently ran a stale test list. New `TEST()`s in an *existing* file need no CMake
edit, but the scrape only reruns when CMake reconfigures — so `cmake --build` alone reported
73/73 passing while five new tests had never run. The suite was green and incomplete at the same
time, which is worse than red. Reconfigure after adding tests.

**Re-baselined the golden hash to `0x51D74AAE506DE6CA`** and regenerated `reference_trace.tsv`.
Every replay recorded before this is invalid. The move is intended and has two causes: towers
serialize a type id where range and damage used to be, and the content fingerprint is now the
first field in the stream. The scripted scenario itself is unchanged and still builds only
tower type 0, so the underlying run is the same run.

**Deliberately left out.** `ItemDef` and `SkillNodeDef` — the phase spec lists them, but there is
nothing to put in them yet and no consumer; ADR 0003's behaviour verbs are Phase 4's work, and an
empty struct written today would fossilise a guess. Damage types, resistances, status effects and
targeting policies are the same argument. The wave schema, the tower/enemy/projectile schemas and
the level schema are complete for everything the simulation currently does.

**Not playtested.** The F5 path builds and its load-order, error-recovery and replay logic are
covered, but nobody has actually pressed the key and watched a run rebalance. The tuning of the
second tower type ("Cannon") is a placeholder, not a balance decision.

**Next:** Phase 4 — systems split, damage pipeline, event queue. It now has data to be written
against rather than constants.

---

## 2026-08-22 — CI ran for the first time, and one ASan bug in the new tests

Phase 2 opened as PR #4, which is what finally triggered CI. Two results.

**The determinism proof landed.** `determinism-compare`: *"All 6 traces agree over 3000 ticks:
macos-debug, macos-release, ubuntu-debug, ubuntu-release, windows-debug, windows-release"*,
and the diff against the committed `reference_trace.tsv` is clean. This was the project's
blocking open item through two phases. ADR 0002's float-with-discipline bet is now measured
rather than assumed, and the `app` job's X11/Wayland package list built on a real runner.

**One job failed, and it was mine.** All six hash tests failed under ASan on Linux while
everything passed on macOS. The cause was in `tests/test_storage.cpp`, not in the simulation:
the allocation counter replaces the global `operator new`, and I replaced the throwing forms
but not the nothrow ones. Deletes are not matched to news by which overload allocated —
ordinary `operator delete` frees nothrow-new memory too — so an ASan-allocated block went into
`std::free` and ASan correctly called alloc-dealloc-mismatch.

What made it platform-specific is the part worth remembering: libstdc++'s `std::stable_sort`
takes its merge buffer from `std::get_temporary_buffer`, which calls
`operator new(n, std::nothrow)`. `World::serialize` sorts entities into canonical id order on
every `checksum()`. libc++ does not use that path, so macOS was green and Linux was red for a
standard-library implementation detail. **The local `asan` preset cannot catch this class on
macOS** — only CI can, which is a decent argument for pushing early rather than polishing
locally.

Fixed by replacing the nothrow forms as well. The over-aligned (`align_val_t`) forms are
deliberately left to the default implementation: they pair only with the aligned deletes, which
are also untouched, so neither half can mismatch — and replacing them would need
`std::aligned_alloc`, which MSVC does not have.

---

## 2026-08-21 — Phase 2: entity storage

Pools, generation handles, a spatial index, and the four known-wrong entries that were waiting
on them. 61 tests pass under `core`, `core-release` and `asan`; the full SDL app builds;
clang-format 20.1.7 is clean.

**The migration held the hash.** One deliberate rebaseline, taken up front and on purpose:
the goal-tunneling fix, projectiles gaining an id, and per-type id counters replacing the
shared one. Everything after that — `Pool` with swap-and-pop, typed handles, the spatial
index, the flow-field direction cache, the placement memo — landed with `determinism_golden`
and all 3,000 ticks of the reference trace **unchanged**. That is the only real evidence the
storage refactor preserved behaviour, and it is worth more than the tests.

The trick that made it possible was serializing in ascending id order instead of array order,
done before any storage changed. Array order is a function of destruction history; id order is
not, and it happened to match the old order exactly. See [ADR 0009](decisions/0009-handles-address-ids-identify.md).

**Three things worth remembering:**

*ADR 0004 was subtly wrong and it took implementing it to see.* It says tie-breaks must resolve
on handle ordering. But a handle's slot index comes off the free list, so it is *also* a
function of destruction history — ordering on handles moves the coupling down a level instead
of removing it. Entities now carry a handle (address, runtime-only, never serialized) and a
monotonic id (identity, the only thing tie-breaks and the byte stream may read). The rule to
hold: nothing in `serialize()` may be a handle, or the first Phase 7 save file is corrupt.

*The golden hash did not catch a real behaviour change, twice.* Caching the BFS parent as the
flow-field direction is the obvious optimisation and it is a **different route** from what
`next()` used to return — the old code returned the first neighbour in `kNeighbours` order that
was one step closer, not whichever cell discovered it. On the default level the two agree, so
the hash stayed green. It would have diverged on the first level with a symmetric detour, and
shown up as an unexplained replay desync months later. The direction is now derived from the
finished distance field, which reproduces the old routing by construction. Same shape of near
miss in the spatial index: clamping only the near end of a query box collapsed off-board boxes
to empty and dropped entities the brute-force scan found. The randomized equivalence test
caught that one; nothing else would have.

*A rule that cannot tie cannot be tested.* `targeting_ties_break_on_handle_not_position` was
written first against a live game and failed on `ticksWithATie > 0` — the current level cannot
produce a tie at all. One lane, one spawn per 45 ticks, one speed per wave, so enemies stay
more than a tile apart and never share a BFS distance. The tie-break is dormant today and
load-bearing the moment there are branching routes or mixed speeds. Targeting moved to
`game/Targeting.h` so the tie could be constructed directly; the live-game half stayed as
`indexed_targeting_matches_bruteforce_in_a_real_run`, which is what would catch a spatial-index
mistake.

**Measured, not asserted** (`stress_5000_enemies`, 3,750 enemies / 200 towers, Release): the
indexed query is 178 us against 423 us for the brute-force scan it replaced, and the real
in-game saving is larger because the old projectile code ran that scan *twice* per projectile.
Debug is 638 us against 10,564. No wall-clock assertion — CI timing is far too noisy to gate
on. What it does assert is that both methods return the identical set at that scale.

**What this phase does not do.** `stress_5000_enemies` drives the real `Pool`, `SpatialIndex`
and targeting rule, but not `World::step()` — nothing today can put 5,000 enemies into a World,
because wave size is a function of the wave number and lives run out first. Stepping the real
world at that scale needs a way to author a starting state, which is Phase 3's content work.

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

*I named the wrong balance change, and an audit caught it.* The tick-conversion commit claimed
towers got ~1.3% slower because 1.6 shots/second is 37.5 ticks rounded to 38. Wrong: the old
float timer already took 38 ticks to expire, so the reload never moved. The timers that
actually shifted were the spawn interval (46 → 45 ticks) and projectile lifetime (181 → 180),
because their float versions summed to just under the nominal duration and needed one extra
tick — while `kTimeBetweenWaves` rounded the other way and landed exactly on 360. Two timers
written identically drifting in opposite directions is the accumulated-error hazard the whole
conversion existed to kill, so the new numbers are right. The lesson is narrower and more
uncomfortable: I asserted a specific quantitative claim in a commit message without measuring
it, and it was wrong in both directions at once. Corrected in `Config.h`, ADR 0007 and here.

**The audit found five more, in code this phase wrote.** Running `determinism-auditor` over
the diff was worth the whole exercise. The worst: the `<cmath>` ban did nothing. `Math.h` is a
header included by `Types.h`, so `<cmath>` is transitively in scope everywhere in `game/`, and
the guard only banned the include directive — the auditor dropped a `std::pow` into
`World.cpp` and every guard reported clean. The guard now bans the functions by name. It also
found three real bugs in the RNG I had just rewritten: `unit()` could return exactly `1.0f`
(128 of 2^32 draws round up past the float's precision), `rangeInt(INT_MIN, INT_MAX)` always
returned `INT_MIN`, and `rangeInt` consumed a draw on some paths and not others. Fixing my own
new code twice in one phase is a useful calibration on how much to trust a careful first pass.

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
