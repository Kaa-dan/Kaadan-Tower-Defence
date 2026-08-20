# 0004 — Pooled storage with generation handles, not an ECS

**Status:** accepted · 2026-08-20

## Context

Entity storage must scale to thousands of entities and survive area damage. The current raw
`std::vector` + pointer approach cannot do either. A data-oriented ECS is the fashionable
answer.

## Decision

**`Pool<T>` with generation-tagged handles, plus a uniform spatial grid. Not an ECS.**

## Why

At thousands of entities across roughly six entity types, pooled storage plus an explicitly
ordered list of system functions matches ECS performance at a fraction of the complexity, and
is dramatically easier to debug.

A full ECS — archetype storage, query language, runtime scheduling — is the classic solo-dev
trap: months of engine work that produces no game. Flagged as the single largest
over-engineering risk in this project.

Handles carry a generation counter, so a stale handle fails safely instead of dangling. Even a
generation wrap is deterministic, so replays stay valid.

## Consequences

- Gameplay tie-breaks must resolve on **handle ordering, never array position** — swap-and-pop
  removal makes array position a function of destruction history.
- System order is fixed at compile time in one readable list, which makes it deterministic for
  free with no scheduler to audit.
- If entity counts ever reach the tens of thousands with heavy per-entity math, revisit. Not
  before.
