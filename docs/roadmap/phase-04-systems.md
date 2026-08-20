# Phase 4 — Systems, damage pipeline, event queue

**Estimate:** 2 days · **Depends on:** Phase 3

## Goal

Give `step()` seams so content can grow without the update loop becoming unreadable, and give
presentation a way to learn what happened without diffing state.

## Acceptance criteria

- [ ] `damage_order_is_independent_of_buff_application_order` — the property that prevents a
      whole class of player-reported balance bugs.
- [ ] `status_effects_stack_per_policy` — Refresh / StackIntensity / StackDuration / Ignore.
- [ ] `events_are_emitted_for_every_kill_and_shot` — no missed FX triggers.
- [ ] `commands_replay_identically` — the same command log produces the same run.
- [ ] `victory_state_is_reachable` — `GameState` currently has no `Victory` at all.
- [ ] `determinism_golden` rebaselined **once**, deliberately, with the change explained.

## Work

**Split `step()`** into free functions over a `SimContext { WorldState&, const ContentDb&,
RngStreams&, EventQueue&, uint32 tick }`. Order is a fixed, readable list in one place —
deterministic for free, with no scheduler to audit.

**Command queue.** `placeTower()` stops being a direct call from `app/`. UI uses a const
`previewPlacement(Cell)` for the hover, enqueues a command on click, and learns the outcome
from an event. **The replay log is exactly this command stream plus tick numbers** — nothing
else needs recording, which is what makes Phase 7 cheap.

**Damage pipeline.** `DamageEvent { EnemyId target; float amount; DamageTypeId type; TowerId
source; }` queued by projectile and status systems, resolved in one place. Fix the multiply
order in code and document it:

`base → + flat mods → × (1 + summed percent) → × status multipliers (ascending StatusDefId
order) → × (1 − resistance) → − armor → clamp ≥ 0`

Ascending `StatusDefId`, **not insertion order** — that makes damage independent of the order
buffs were applied, which is both a determinism property and the thing players otherwise file
bug reports about.

**Status effects** — fixed-size inline array on the entity (`std::array<StatusSlot, 8>`). No
heap, deterministic iteration.

**Event queue** — POD events drained by presentation each frame. **Rule: events are write-only
from the simulation's perspective**; nothing in `game/` may read them. Include them in the
state hash anyway — a divergence in *why* something happened is worth catching.

**`StatMod` layer** for skills and items: `{ StatId, ModOp, float }`. Cache resolved tower
stats behind a dirty flag; recompute on mod-set change, not per frame. Roughly 20-30 code-level
verbs plus numbers covers 100+ combinable items.

**Also:** `previousPosition` on Enemy and Projectile (Phase 6 needs it), game speed 1x/2x/3x,
`GameState::Victory` and `Paused`.
