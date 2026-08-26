#pragma once

// Every field a run depends on, in one plain struct.
//
// Phase 4 split the simulation's *behaviour* into free functions (Systems.h).
// That split only pays off if the functions have something to be free of: as
// long as the fields were private members of World, every new rule had to
// become a new method on a class that was already four hundred lines long, and
// "which of these thirty members does stepTowers touch" was unanswerable
// without reading it.
//
// So the state is a struct with public fields and no invariants, and the rules
// are functions over it. World is the lid on top: it owns one of these, runs the
// systems in a fixed order, and exposes a read-only view to the renderer.
//
// The canonical serializer lives here rather than on World for the reason it
// always has -- a serializer kept away from the fields it writes is a
// serializer that gets a field added behind its back. Adding a member to this
// struct means adding a line to serialize().

#include "game/Commands.h"
#include "game/Damage.h"
#include "game/Entity.h"
#include "game/Events.h"
#include "game/Grid.h"
#include "game/Path.h"
#include "game/Pool.h"
#include "game/Rng.h"
#include "game/SpatialIndex.h"
#include "game/Types.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ktd {

enum class GameState : std::uint8_t {
    Playing = 0,
    Defeat = 1,
    // The player stopped the clock. A paused tick is a tick that never
    // happened -- step() returns before the counter moves -- so pausing cannot
    // change a run and is deliberately not a recorded command.
    Paused = 2,
    // Every authored wave cleared, on a level whose content declares no endless
    // rule. A level that does declare one never reaches this, which is what the
    // shipped content does today.
    Victory = 3,
};

namespace detail {

// The entities of one type in canonical order: ascending id, independent of
// where they happen to sit in storage.
//
// This is what lets Phase 2 swap std::vector for a swap-and-pop pool without
// moving the hash. Array position is a function of destruction history; id is
// not. Serializing in array order would have made every removal reorder the
// byte stream, and the golden hash -- the only evidence that the storage
// refactor preserved behaviour -- would have failed for a reason that has
// nothing to do with behaviour.
//
// stable_sort, not sort: the purity guard bans std::sort because the three
// standard libraries pick different introsort pivots and order equal elements
// differently. Ids are unique here, so it is a belt-and-braces choice, but the
// rule is worth keeping unconditional.
template <typename T>
std::vector<const T*> byId(Span<const T> items) {
    std::vector<const T*> ordered;
    ordered.reserve(items.size());
    for (const T& item : items) {
        ordered.push_back(&item);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const T* a, const T* b) { return a->id < b->id; });
    return ordered;
}

}  // namespace detail

struct WorldState {
    // Cached rather than recomputed, because checksum() is called every tick by
    // the determinism trace and the database does not change under a running
    // world -- a hot reload builds a new one.
    std::uint64_t contentFingerprint = 0;

    Grid grid{1, 1};
    FlowField flow;
    Cell spawn{};
    Cell goal{};

    // Each pool owns its own monotonic id sequence, rather than the three of
    // them drawing from one shared counter. Shared meant that adding ids to
    // projectiles renumbered every enemy and tower, because they came out of the
    // same sequence -- a format change in one entity type silently rewriting the
    // identities of the others.
    Pool<Enemy, EnemyTag> enemies;
    Pool<Tower, TowerTag> towers;
    Pool<Projectile, ProjectileTag> projectiles;

    // Rebuilt from enemies once per tick, before the towers pick targets.
    SpatialIndex<Enemy> enemyIndex;

    // Intent in, facts out. See Commands.h and Events.h.
    CommandQueue commands;
    EventQueue events;
    // This tick's pending damage, sorted before it is drained. See Damage.h.
    DamageQueue damage;

    // Scratch, reserved once and reused. step() must not allocate.
    std::vector<EnemyId> enemyRemovals;
    std::vector<ProjectileId> projectileRemovals;

    // Bumped whenever a tower changes the grid, which is the only thing that can
    // change a placement answer. World's preview memo is keyed on it.
    std::uint32_t gridVersion = 0;

    RngStreams rng{0};

    // The simulation clock. Everything time-dependent counts ticks, not
    // seconds, so a run is reproducible by construction rather than by
    // convention.
    std::uint64_t tick = 0;

    int gold = 0;
    int lives = 0;
    int wave = 0;
    // Compounding difficulty for the wave in progress, recomputed only when a
    // wave starts. See sys::waveDirector.
    float waveHealthScale = 1.0f;
    float waveSpeedScale = 1.0f;
    int enemiesLeftToSpawn = 0;
    int spawnedInWave = 0;
    std::uint32_t spawnTimer = 0;
    std::uint32_t waveTimer = 0;
    int leaked = 0;
    int killed = 0;
    GameState state = GameState::Playing;

    // The world's canonical byte stream: every piece of state that a
    // reproducible run depends on, in a fixed order, written as explicit
    // little-endian integers.
    //
    // Sink supplies u8/u32/u64/i32/f32. Templated so the same field order
    // serves the determinism hash now and the Phase 7 save writer later -- two
    // separate implementations of one format would drift.
    //
    // Field order IS the format. Appending is safe; reordering or removing
    // changes every hash, which is what simVersion will exist to notice.
    template <typename Sink>
    void serialize(Sink& out) const {
        // Which ruleset this run was played under, first.
        //
        // Balance is now data, so two runs with the same seed and the same
        // commands are only the same run if they were played against the same
        // numbers. Without this the hash would call a rebalanced replay
        // identical right up to the tick the change first mattered. Hot reload
        // does exactly that on purpose; a stale save file does it by accident.
        out.u64(contentFingerprint);

        out.u64(tick);
        out.i32(gold);
        out.i32(lives);
        out.i32(wave);
        out.i32(leaked);
        out.i32(killed);
        out.u32(static_cast<std::uint32_t>(state));
        out.u32(enemies.nextSequence());
        out.u32(towers.nextSequence());
        out.u32(projectiles.nextSequence());

        // Derived from the grid dimensions today, so hashing the dimensions
        // already covers them. Written explicitly anyway: the moment a level
        // loader sets a spawn or goal that is not a function of the size, this
        // stops being true, and goal drives the flow field that every enemy
        // steers by.
        out.i32(spawn.x);
        out.i32(spawn.y);
        out.i32(goal.x);
        out.i32(goal.y);

        // Wave director. Invisible to the player and decisive for the run.
        out.i32(enemiesLeftToSpawn);
        // How far into this wave's composition the spawner has got. Decisive
        // once a wave mixes enemy types, and invisible in a wave that does not.
        out.i32(spawnedInWave);
        out.u32(spawnTimer);
        out.u32(waveTimer);
        out.f32(waveHealthScale);
        out.f32(waveSpeedScale);

        out.u32(static_cast<std::uint32_t>(enemies.size()));
        for (const Enemy* p : detail::byId<Enemy>(enemies.items())) {
            const Enemy& enemy = *p;
            out.u32(enemy.id);
            out.f32(enemy.position.x);
            out.f32(enemy.position.y);
            out.f32(enemy.previousPosition.x);
            out.f32(enemy.previousPosition.y);
            out.f32(enemy.health);
            out.f32(enemy.maxHealth);
            out.f32(enemy.speed);
            out.u32(enemy.type);
            serializeStatuses(out, enemy.statuses);
        }

        out.u32(static_cast<std::uint32_t>(towers.size()));
        for (const Tower* p : detail::byId<Tower>(towers.items())) {
            const Tower& tower = *p;
            out.u32(tower.id);
            out.i32(tower.cell.x);
            out.i32(tower.cell.y);
            // The type, where range and damage used to be written. Those are
            // no longer state -- they live in the database, which the
            // fingerprint above covers.
            out.u32(tower.type);
            out.u32(tower.cooldown);
            // aim is rendering-only, but it is derived from simulation state
            // and costs two words to cover. A field left out of the hash is a
            // field that can diverge silently.
            out.f32(tower.aim.x);
            out.f32(tower.aim.y);
            // The mods are state; the resolved stats are a cache of (mods,
            // def) and are deliberately absent, on the same argument that keeps
            // the flow field out. If they could disagree with the mods that
            // would be the bug, and hashing them would hide it behind a
            // divergence in something that is not state.
            serializeMods(out, tower.mods);
        }

        out.u32(static_cast<std::uint32_t>(projectiles.size()));
        for (const Projectile* p : detail::byId<Projectile>(projectiles.items())) {
            const Projectile& shot = *p;
            out.f32(shot.position.x);
            out.f32(shot.position.y);
            out.f32(shot.previousPosition.x);
            out.f32(shot.previousPosition.y);
            out.f32(shot.velocity.x);
            out.f32(shot.velocity.y);
            out.u32(shot.id);
            out.u32(shot.targetId);
            out.u32(shot.sourceId);
            out.u32(shot.type);
            out.u32(shot.damageType);
            out.f32(shot.damage);
            out.u32(shot.age);
        }

        // The grid is state, not level constants: towers change it as they are
        // built, and the flow field is derived from it.
        out.i32(grid.width());
        out.i32(grid.height());
        for (int y = 0; y < grid.height(); ++y) {
            for (int x = 0; x < grid.width(); ++x) {
                out.u8(static_cast<std::uint8_t>(grid.at({x, y})));
            }
        }

        // flow is deliberately absent. It is a pure function of (grid, goal),
        // both of which are written above, and it is rebuilt on the only path
        // that mutates the grid. So it cannot diverge independently -- this is
        // one of the two exceptions to the rule stated at the top of this
        // function, the other being the tower stat cache.
        //
        // It does matter for Phase 7: a save writer using this stream will not
        // persist the distance cache, so the loader must call
        // flow.build(grid, goal) after restoring the grid, or every enemy
        // stands still.

        // Commands that have been enqueued and not yet drained. A world saved
        // between a click and the next step owes the player that tower.
        commands.serialize(out);

        // This tick's events. Write-only for the simulation, hashed anyway: a
        // run that reaches the same state for a different reason has a bug in
        // it, and this is the cheapest place to notice.
        events.serialize(out);

        // The damage queue is absent because it is empty here by construction:
        // it is filled and fully drained inside a single step(), and never
        // survives across the tick boundary where the hash is taken.

        // Every stream's position, including the ones nothing has drawn from
        // yet. Two worlds that look identical but sit at different points in a
        // stream are different runs -- they diverge on the next draw.
        for (std::size_t i = 0; i < rng.size(); ++i) {
            out.u64(rng.at(i).state());
        }
    }
};

}  // namespace ktd
