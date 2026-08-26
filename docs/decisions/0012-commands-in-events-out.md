# 0012 — Commands in, events out

**Status:** accepted · 2026-08-26

## Context

Before Phase 4 the UI called `World::placeTower()` directly from its click handler, read the
`PlacementResult` back, and — separately, on the next line — appended an entry to a command log
so that hot reload could replay the run. Two things had to be kept in step by hand, and the
second one was easy to forget: every future player action (sell, upgrade, target priority,
ability) would need its own "and also write it down".

In the other direction, presentation had no way to learn that anything had happened. The
renderer drew the current state and nothing else, so a muzzle flash or a death effect would have
meant diffing state between frames — presentation reimplementing the simulation's rules, badly,
and getting it wrong the first time a kill and a spawn landed on the same tick.

## Decision

**The only way into the simulation is a command queue. The only way out is an event queue.**

- `World::enqueue(Command)` queues intent; `sys::commands` drains it at the top of the next
  `step()` and reports the outcome as a `TowerPlaced` or `TowerRejected` event.
- The UI asks `previewPlacement(Cell, TowerDefId)` — const, memoized — for its hover, so a
  preview cannot accidentally become a placement.
- The event queue is **cleared at the top of every `step()`** and holds exactly one tick.
- **Events are write-only from the simulation's perspective.** Nothing in `game/` may read the
  queue back.
- Events are included in the state hash anyway.

`placeTower()` stays public for tests and the scripted determinism scenario, which want the
answer on the spot. `app/` does not call it.

## Why

- **The replay log becomes the command stream plus tick numbers, and nothing else.** That is what
  makes Phase 7 cheap: a replay file is a serialization format and not a second input path to
  audit. There is no way to affect a run that bypasses the recording, because there is no second
  way in.
- **Write-only events keep the queue a report rather than a message bus.** The moment a rule
  branches on an event, the tick's behaviour starts depending on how far the consumer had got
  through draining it, and the event order becomes something that has to be audited for
  determinism.
- **One tick's worth, cleared at the top, is what keeps events hashable.** An accumulating queue
  would make the checksum depend on how often the caller happened to drain it — a frame-rate
  dependency in the determinism trace.
- **Hashing events catches divergence in *why*.** Two runs that reach the same state for
  different reasons are not the same run, and one of them has a bug. This is the cheapest place
  to notice.

## Consequences

- Presentation must drain events **per step, not per frame**. A frame that fast-forwards three
  ticks has three ticks of events, and a per-frame read would see only the last.
- A placement now lands on the tick *after* the click rather than during it. At 60 Hz that is
  16 ms and nobody can feel it; it is what makes the click's tick number meaningful.
- Pausing is deliberately **not** a command. A paused tick is a tick that never happened —
  `step()` returns before the counter moves — so pause cannot change a run and recording it would
  put noise in every replay file.
- The command queue is bounded at 64 pending actions and refuses beyond that, rather than
  growing. An unbounded queue is an allocation on a gameplay path and a way for a malformed
  replay file to exhaust memory.
