#pragma once

// Entity identity and the three entity types.
//
// Split out of World.h in Phase 4 so that the event queue, the damage pipeline
// and the status system can name an enemy without including the whole world --
// which they cannot do, because World includes them.

#include "game/Pool.h"
#include "game/Stats.h"
#include "game/Status.h"
#include "game/Types.h"
#include "game/content/Database.h"

#include <array>
#include <cstdint>

namespace ktd {

// The stable identity of an entity: a monotonic per-pool sequence number, unique
// for the lifetime of a run and never recycled.
//
// Distinct from the storage handle on purpose, and the distinction is the
// load-bearing idea of Phase 2. A handle names a slot; an id names an
// entity. See ADR 0009.
using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidId = 0;

// Tags exist only to make the handle types distinct, so a TowerId cannot be
// passed where an EnemyId belongs.
struct EnemyTag {};
struct TowerTag {};
struct ProjectileTag {};

using EnemyId = Handle<EnemyTag>;
using TowerId = Handle<TowerTag>;
using ProjectileId = Handle<ProjectileTag>;

// Entities carry a def id and read their stats from the database, rather than
// baking the numbers in when they are created.
//
// The difference is invisible until you press F5. Baked stats mean a balance
// change only affects entities built afterwards, so a hot reload of a run in
// progress produces a world half-governed by the old numbers. A def id means
// there is exactly one copy of every number and it lives in the database.
//
// Per-entity state -- health, cooldown, position -- still lives on the entity,
// because that is state rather than balance.

struct Enemy {
    EntityId id = kInvalidId;
    Vec2 position{};
    // Where this enemy stood at the end of the previous tick.
    //
    // The simulation never reads it. It exists because the renderer draws at
    // display rate and the simulation advances at 60 Hz, so a frame almost
    // never lands on a tick boundary and has to interpolate between the two
    // most recent positions (Phase 6, known-wrong #5). Recording it here is
    // free; reconstructing it later is not, because by the time presentation
    // runs the previous tick is gone.
    Vec2 previousPosition{};
    float health = 0.0f;
    // Kept alongside the def because the wave multiplies it: two grunts from
    // different waves share a type and not a maximum.
    float maxHealth = 0.0f;
    float speed = 0.0f;
    content::EnemyDefId type = content::kNoDef;

    // Fixed-size and inline. A std::vector here would put a heap allocation on
    // the path of every applied debuff and make iteration order a function of
    // allocator behaviour; eight slots is more statuses than any design in ADR
    // 0003's verb budget can produce at once, and overflow is dropped
    // deterministically rather than growing.
    std::array<StatusSlot, kStatusSlots> statuses{};
};

struct Tower {
    EntityId id = kInvalidId;
    Cell cell{};
    content::TowerDefId type = content::kNoDef;
    std::uint32_t cooldown = 0;  // ticks until it may fire again
    Vec2 aim{1.0f, 0.0f};        // facing, for rendering only

    // Modifiers contributed by skills and items. Inline for the same reason the
    // status slots are.
    ModSet mods{};
    // Resolved stats, recomputed when the mod set changes rather than per
    // frame. `statsDirty` starts true so the first read resolves.
    TowerStats stats{};
    bool statsDirty = true;
};

struct Projectile {
    EntityId id = kInvalidId;
    Vec2 position{};
    Vec2 previousPosition{};
    Vec2 velocity{};

    // The target is named twice, and the redundancy is deliberate.
    //
    // `target` is the storage handle: it makes the per-tick lookup O(1) and
    // fails safely once the enemy dies, which is what removed the double linear
    // scan this used to do (known-wrong #3). But a handle is a slot address, and
    // slot addresses are a function of the pool's destruction history -- so a
    // handle must not be written to a save file, because Phase 7 will restore
    // into pools whose slots are assigned differently and the handle would then
    // name the wrong enemy.
    //
    // `targetId` is the stable identity, and it is the one the canonical
    // serialization writes. Both are set once when the shot is fired and never
    // change, so they cannot drift apart.
    EnemyId target{};
    EntityId targetId = kInvalidId;
    content::ProjectileDefId type = content::kNoDef;
    // Carried on the shot rather than read from the tower, because the tower
    // may be sold before the shot lands. This is the tower's *resolved* damage:
    // the flat and percent stages of the damage formula have already run, and
    // what remains -- statuses, resistance, armor -- belongs to the target and
    // is applied when the hit lands. See ADR 0011.
    float damage = 0.0f;
    content::DamageTypeId damageType = content::kNoDef;
    // The effect this shot applies on impact, kNoDef for none. Carried rather
    // than read back from the tower, for the same reason the damage is.
    content::StatusDefId appliesStatus = content::kNoDef;
    // Who fired it, so the damage pipeline and the kill event can attribute the
    // hit. The handle may already be stale if the tower was sold; the id is what
    // gets serialized.
    TowerId source{};
    EntityId sourceId = kInvalidId;
    std::uint32_t age = 0;  // ticks since it was fired
};

}  // namespace ktd
