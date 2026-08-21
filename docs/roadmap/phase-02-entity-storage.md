# Phase 2 — Entity storage

**Estimate:** 3 days · **Depends on:** Phase 1 (the state hash is what validates this work)

## Goal

Storage that survives area damage and scales to thousands of entities. Pooled storage with
generation-tagged handles, not an ECS —
[ADR 0004](../decisions/0004-pools-not-ecs.md).

## Acceptance criteria

- [x] `pool_stale_handle_returns_null` — a handle to a destroyed entity fails safely.
- [x] `pool_survives_generation_wrap` — deterministically, even when the counter wraps.
- [x] `spatial_index_matches_bruteforce` — the grid query returns exactly what an O(N) scan
      would, over randomized layouts.
- [x] `targeting_ties_break_on_handle_not_position` — the property that makes the refactor
      hash-invariant. Ties break on the stable entity **id**, not on the handle: a handle's
      slot index comes off the free list and is therefore also a function of destruction
      history. See [ADR 0009](../decisions/0009-handles-address-ids-identify.md).
- [x] `placement_preview_allocates_nothing` — the per-frame hover query stops copying the grid.
- [x] `stress_5000_enemies` — reports timing; **does not assert wall-clock** (CI timing is too
      noisy to gate on). Drives the real `Pool`, `SpatialIndex` and targeting rule but **not
      `World::step()`** — nothing today can put 5,000 enemies into a World. See the ledger.
- [x] `determinism_golden` **unchanged** through steps 3-6 below — held, along with all 3,000
      ticks of the reference trace. This is the real criterion:
      an unchanged hash proves the refactor was behavior-preserving in a way passing tests
      cannot.
- [x] ASan/UBSan preset clean.

## Migration order (this is the whole trick)

Each step is individually verifiable, and steps 3-6 must leave the hash **unchanged**:

1. **`Span` first, storage second.** Add `game/include/game/Span.h` and change the accessors at
   `World.h:71-73` from `const std::vector<X>&` to `Span<const X>` **while the members are
   still `std::vector`**. Every call site uses only range-for, `.size()`, `[]`, `.empty()` — so
   this is a drop-in with zero behavior change.
2. **Handle-based tie-breaks** in targeting. A *deliberate* behavior change — rebaseline the
   golden hash here, on purpose, in its own commit.
3. Swap `std::vector<Enemy>` → `Pool<Enemy, EnemyTag>`. Call sites untouched. **Hash unchanged.**
4. `EntityId` → typed handles; delete `findEnemy`'s linear scan. **Hash unchanged.**
5. Add `EnemyIndex`; targeting queries it. **Hash unchanged** — same candidate set, handle-ordered ties.
6. Flow-field direction cache, placement memoization, `reserve()`. **Hash unchanged.**

## Work

- **`Pool<T, Tag>`** — dense array + sparse slot table + **FIFO** free list (LIFO hammers a few
  slots and wraps generations faster). Handle is 20-bit index | 12-bit generation, typed by tag
  so a `TowerId` cannot be passed where an `EnemyId` belongs.
- **A debug-only mutation epoch** on the pool: bump on create/destroy, assert unchanged on
  borrow dereference. ~20 lines, and it permanently kills the `findTarget` dangling-pointer
  class that exists today.
- **`EnemyIndex`** — uniform tile-aligned buckets, rebuilt per tick by **counting sort**
  (O(N), allocation-free, stable, therefore deterministic by construction). Not a quadtree.
- **Kill the O(P×E) scan** — projectiles get ids and live in a pool; `targetId` is an
  `EnemyId`; lookup is O(1) and happens **once**, not twice.
- **`killedThisStep`** becomes a reserved member cleared per tick, not constructed per frame.
- **Flow field stores directions**, not just distances.
- **`canPlaceTower`** stops copying the `Grid`: set tile → BFS into a reused scratch field →
  restore. Then memoize on `(cell, gridVersion)`.
- **Fix the goal tunneling bug** (known-wrong #6) — radius test → cell test.

## Out of scope

Content, systems, rendering.
