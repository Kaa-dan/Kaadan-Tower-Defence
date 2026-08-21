# 0009 — Handles address storage, ids identify entities

**Status:** accepted · 2026-08-21

## Context

[ADR 0004](0004-pools-not-ecs.md) chose pooled storage with generation-tagged handles, and
required that "gameplay tie-breaks must resolve on **handle ordering**, never on array
position", because swap-and-pop makes array position a function of destruction history.

Implementing that in Phase 2 surfaced a problem with the letter of it. A handle is a slot
index plus a generation, and *which slot* an entity lands in is also a function of destruction
history — the free list hands back whatever was released earliest. Ordering on handles would
have moved the coupling ADR 0004 set out to remove down one level rather than removing it.

Three things need an answer to "which entity is this?", and they do not want the same answer:

- the per-tick target lookup, which wants O(1) and wants a dead target to fail safely,
- gameplay tie-breaks, which want an order that does not change when an unrelated entity dies,
- the hash and the Phase 7 save format, which want something that survives being written to
  disk and read back into a differently-laid-out pool.

## Decision

**Every entity carries both.** A `Handle<Tag>` is its *address*; a monotonic `EntityId`
stamped by its pool is its *identity*.

- Handles are runtime-only. They are never serialized, and never decide a gameplay outcome.
- Ids are monotonic per pool, never recycled, and are the sole key for tie-breaks, for
  canonical serialization order, and for anything written to a save file.

`Projectile` carries both for its target: `target` (handle, for the O(1) lookup) and
`targetId` (id, for the byte stream).

## Why

**A handle in a save file is a bug waiting for Phase 7.** Loading a save rebuilds the pools,
and a restored pool assigns slots by replaying the load, not by replaying the original run's
destruction history. A persisted handle would then name a different enemy — or a live one that
happens to occupy the recycled slot with a matching generation. Ids do not have this problem
because they are never reused.

**It made the phase's own acceptance criterion reachable.** The migration was supposed to
leave the determinism hash untouched, which is the only real evidence that swapping
`std::vector` for a pool changed no behaviour. Serializing in array order could not do that —
swap-and-pop reorders the array on every removal. Serializing in ascending id order matches
the old order exactly today (the vector was only ever appended to) and stays canonical
afterwards. The entire storage refactor then landed with the golden hash and all 3,000 ticks
of the reference trace unchanged.

**One monotonic sequence per pool, not one shared counter.** With a shared counter, giving
projectiles an id renumbered every enemy and tower, because all three drew from it — a format
change to one entity type silently rewriting the identities of the others.

## Consequences

- Four bytes per entity for the id, and two fields on `Projectile` that must agree. They are
  written once at construction and never change, so they cannot drift.
- The rule to hold the line on: **nothing in `serialize()` may be a handle.** A handle written
  to the byte stream would pass every test in this phase and corrupt the first save file.
- ADR 0004's tie-break clause should be read as "never array position, always stable
  identity". Its reasoning is right; "handle ordering" was the imprecise part.
- Targeting moved out of `World` into `game/Targeting.h` to make the tie-break testable. The
  current level cannot produce a tie — one lane, one spawn per 45 ticks, one speed per wave, so
  enemies are strung out more than a tile apart and never share a BFS distance — so as a
  private method the rule was only reachable by a test that could not exercise it.
