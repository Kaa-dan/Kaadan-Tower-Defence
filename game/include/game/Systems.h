#pragma once

// The tick, as an ordered list of free functions.
//
// step() used to be four private methods on World, which was fine at four and
// would not have been fine at twelve: every new rule meant another method on a
// class that already owned thirty fields, and nothing in the signature said
// which of them a given method was allowed to touch.
//
// These take a SimContext instead. The order they run in is a single readable
// list in World::step() -- **fixed at compile time, not scheduled**. ADR 0004
// chose that deliberately: a runtime scheduler with dependencies and priorities
// is a thing that has to be audited for determinism, and a list of calls is a
// thing that is deterministic by being obvious.

#include "game/Commands.h"
#include "game/Events.h"
#include "game/WorldState.h"
#include "game/content/Database.h"

namespace ktd {

// Everything a system is allowed to touch, and nothing else.
//
// `rng` and `tick` are views onto fields of `state` rather than separate
// storage. They are named here because reading `ctx.rng[RngStream::Crit]` at
// the point of use says what is happening and `ctx.state.rng[...]` does not,
// and because `tick` is fixed for the duration of the tick -- the systems see
// one number, not a counter that could move under them.
struct SimContext {
    SimContext(WorldState& worldState, const content::ContentDb& contentDb)
        : state(worldState),
          content(contentDb),
          rng(worldState.rng),
          events(worldState.events),
          tick(worldState.tick) {}

    WorldState& state;
    const content::ContentDb& content;
    RngStreams& rng;
    EventQueue& events;
    std::uint64_t tick;
};

namespace sys {

// The tick's systems, in the order World::step() runs them. The order is the
// design: an enemy that moved after the towers looked for it would be shot at
// where it used to be, and damage resolved before the projectiles moved would
// land a tick early.

// Drains the command queue: the only way anything outside the simulation gets
// in. Emits TowerPlaced or TowerRejected for each.
void commands(SimContext& ctx);

// Starts waves, spawns enemies, counts down the gap.
void waveDirector(SimContext& ctx);

// Ages every live status effect, expires the finished ones, and queues damage
// for the ones that tick.
void statuses(SimContext& ctx);

// Moves every enemy along the flow field and leaks the ones that reach the
// goal.
void enemies(SimContext& ctx);

// Rebuilds the spatial index. Between the enemies moving and the towers
// looking, so a tower never targets an enemy at last tick's position.
void index(SimContext& ctx);

// Cooldowns, targeting, aiming, firing.
void towers(SimContext& ctx);

// Moves shots, tests for impact, queues damage. Does not apply it.
void projectiles(SimContext& ctx);

// Sorts the tick's damage into canonical order, resolves it through the one
// formula, and destroys whatever it killed.
void damage(SimContext& ctx);

// Defeat and victory.
void outcome(SimContext& ctx);

// --- shared helpers, exposed because tests and the placement preview need
// them ----------------------------------------------------------------------

// The stats a tower fights with this tick, resolving and caching them if its
// mod set has changed since the last call.
const TowerStats& towerStats(Tower& tower, const content::ContentDb& content);

// Applies one placement, immediately. sys::commands is its only caller in a
// running game; World exposes it for tests and for the scenario harness.
PlacementResult placeTower(SimContext& ctx, Cell cell, content::TowerDefId type);

// How many enemies wave `wave` sends, and which type the nth spawn of the
// current wave is.
int enemiesInWave(const content::ContentDb& content, int wave);

}  // namespace sys

}  // namespace ktd
