# Phase 1 — Determinism hardening

**Estimate:** 2 days · **Depends on:** Phase 0 · **Blocks:** Phase 2

## Goal

Make the simulation bit-identical across macOS, Windows, and Linux, and prove it with a test
that cannot pass by accident. This is the most expensive property to retrofit, which is why it
comes first.

The approach is float-with-discipline, not fixed-point —
[ADR 0002](../decisions/0002-float-not-fixed-point.md) has the reasoning. Do not reopen it
without reading that first.

## Acceptance criteria

Each names a test. The phase is done when they pass and not before.

- [ ] `determinism_golden` — 3,000 steps from a fixed seed produce a `World::checksum()` equal
      to a constant committed in the test. Fails loudly if any gameplay change alters it.
- [ ] `determinism_cross_platform` — the CI job runs `ktd_hashdump` on all three OSes, uploads
      each `tick→hash` trace, and a compare job diffs them. **A failure must name the first
      divergent tick**, not merely report inequality. This is the criterion that actually
      proves the phase; the rest is scaffolding for it.
- [ ] `determinism_debug_matches_release` — the hash trace is identical between Debug and
      RelWithDebInfo on one OS. This is the FMA-contraction canary and usually breaks first.
- [ ] `math_powi_matches_pow_for_small_exponents` — the replacement is correct, not merely
      deterministic.
- [ ] `rng_streams_are_independent` — adding a consumer to one stream does not alter another's
      output.
- [ ] `rng_rangeint_is_unbiased` — a chi-squared or bucket-count check over a large sample.
- [ ] `purity_determinism` passes with `ENFORCE_CMATH=ON`.
- [ ] All 24 existing tests still pass.

## Work

**`game/include/game/Math.h` + `Math.cpp`** — the single audited math surface.
`sqrt`, `floor`, `floorToInt` (range-clamped, killing the UB), `abs`, `min`, `max`, `clamp`,
`powi` (exponentiation by squaring). Later: `sinTurns`/`cosTurns` via table, `atan2Approx`.
After this, `<cmath>` is illegal everywhere else in `game/` — flip `ENFORCE_CMATH` on in
`tests/CMakeLists.txt`.

**Replace `std::pow`** (`game/src/World.cpp:74-75`). Do not call `powi` per spawn — compute
the wave scale **once when the wave starts** and store it in wave state. Note honestly in the
commit that this shifts balance numbers by an ulp versus `std::pow`; that is fine now and must
be locked before replays ship.

**Integer tick counters.** `cooldown`, `spawnTimer_`, `waveTimer_`, `age` become `uint32` tick
counts. Then **remove `dt` from `step()` entirely** — `void World::step()` with `tick_` as the
clock. A parameter nothing enforces is a permanent invitation to break reproducibility. Fix
the call sites in `tests/` and `app/main.cpp` in the same commit. `kFixedDt` stays a float for
spatial math (`speed * dt`); the app-side accumulator uses a separate `double`, and the two
never mix.

**`game/include/game/Rng.h`** — extract `Rng` out of `Types.h`. Add splitmix64 seed mixing
(`Rng(1)`, `Rng(2)`, `Rng(3)` currently produce correlated output), Lemire's method for
`rangeInt` (fixes modulo bias), and a `state()` accessor for hashing. Add `RngStreams` with one
stream per named consumer:

```cpp
enum class RngStream : uint32_t {
    WaveComposition = 1, SpawnJitter = 2, Targeting = 3,
    Crit = 4, Loot = 5, StatusProc = 6, Cosmetic = 7,   // append only, never renumber
};
```

Be honest about the limit: substreams protect against *adding* consumers. Changing the number
or order of draws within a stream still desyncs old replays — that is what `simVersion` is for
in Phase 7.

**`cmake/KtdDeterminism.cmake`** — `-ffp-contract=off -fno-fast-math` (GCC/Clang),
`/fp:precise /fp:contract-` (MSVC), applied `PUBLIC` on `ktd_game`. Plus a **deny check** that
`FATAL_ERROR`s if `-ffast-math`, `-Ofast`, or `/fp:fast` appear in any flags variable —
someone will try `-Ofast` for a perf experiment at some point. Add `static_assert`s for
`is_iec559` and `FLT_EVAL_METHOD == 0`.

**`game/include/game/Hash.h`** — `hashWorld()`. **Hash a canonical serialization, never raw
struct bytes** — padding is not portable. Write the serializer here and reuse it for saves in
Phase 7, so it is exercised on every test run rather than rotting until someone tries to load
a save.

**`tests/tools/hashdump.cpp`** → `ktd_hashdump`. Runs a fixed scripted command sequence,
prints `tick<TAB>hash` per line. Links only `ktd_game`, so it builds in seconds without SDL.

**CI** — add `determinism` (matrix of 3 OSes, uploads traces) and `determinism-compare`
(`needs:` the first, diffs the artifacts, names the first divergent tick).

**Turn on `-Wconversion` and `-Wdouble-promotion`** here, in the same pass that touches the
math. Both are determinism-relevant; both are noisy against existing casts. Doing it now means
the churn lands with the work it belongs to.

## Out of scope

Entity storage, content, systems. If you find a storage bug, log it in the ledger's known-wrong
table and move on.

## Watch out for

- The existing `identical_seeds_produce_identical_runs` compares two worlds **in the same
  process** with a `1e-6` tolerance. It cannot catch cross-platform divergence. Do not treat it
  as evidence.
- Run the hash dump under Debug *and* Release before declaring victory.
- Run the `determinism-auditor` agent over the diff before shipping.
