# Phase 7 — Save, replay, settings

**Estimate:** 2 days · **Depends on:** Phases 1 and 4

## Goal

Ship replays as a feature. Determinism makes this cheap — most of the machinery already exists
from Phase 1's hash serializer and Phase 4's command queue.

## Acceptance criteria

- [ ] `replay_roundtrips_to_identical_final_hash` — record, save, load, replay, compare.
- [ ] `snapshot_roundtrips_all_state` — including **RNG stream states and handle generation
      counters**, which are the fields everyone forgets.
- [ ] `replay_with_mismatched_sim_version_is_rejected` — cleanly, not silently mis-replayed.
- [ ] `replay_with_mismatched_content_hash_is_rejected`.
- [ ] `golden_replays_still_pass` — a directory of recorded replays run in CI. **This becomes
      the most valuable test asset in the project**: every gameplay bug fixed turns into a
      permanent recorded regression.

## Both formats, for different jobs

**Input-log replay (`.ktdr`) — the shipping feature.** Header: magic, `formatVersion`,
`simVersion`, `contentHash`, `masterSeed`, `levelId`, build SHA. Body: varint tick delta +
command bytes. Footer: final stats. A 30-minute run is a few hundred commands, well under 10 KB.

**Full-state snapshot — the save-game feature.** Input-log resume is the wrong tool for "quit
and come back tomorrow": it re-simulates from tick 0 and breaks the moment balance is patched.

Explicit field-by-field serialization. **Never `memcpy` a struct** — padding is not portable and
would silently poison the state hash.

## Versioning

- `formatVersion` bumps on serializer layout change. **Write no migrations before 1.0** —
  invalidate saves and say so in the patch notes. Write migrations when there are players who
  would be upset.
- `simVersion` bumps on any gameplay-logic change; mismatched replays are **refused**. Every
  game with replays does this. Pretending otherwise produces silently-wrong playback, which is
  far worse than a clean refusal. The footer keeps final stats, so a stale replay still renders
  a valid leaderboard entry — you just cannot watch it.
- `contentHash` mismatch gets the same treatment. Data changes *are* sim changes.
