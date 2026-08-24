#pragma once

#include "game/Config.h"
#include "game/Grid.h"
#include "game/Path.h"
#include "game/Pool.h"
#include "game/Rng.h"
#include "game/Span.h"
#include "game/SpatialIndex.h"
#include "game/Targeting.h"
#include "game/Types.h"

#include <algorithm>
#include <vector>

namespace ktd {

// The stable identity of an entity: a monotonic per-pool sequence number, unique
// for the lifetime of a run and never recycled.
//
// Distinct from the storage handle on purpose, and the distinction is the
// load-bearing idea of this phase. A handle names a slot; an id names an
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

struct Enemy {
    EntityId id = kInvalidId;
    Vec2 position{};
    float health = 0.0f;
    float maxHealth = 0.0f;
    float speed = 0.0f;
};

struct Tower {
    EntityId id = kInvalidId;
    Cell cell{};
    float range = config::kTowerRange;
    float damage = config::kTowerDamage;
    std::uint32_t cooldown = 0;  // ticks until it may fire again
    Vec2 aim{1.0f, 0.0f};        // facing, for rendering only
};

struct Projectile {
    EntityId id = kInvalidId;
    Vec2 position{};
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
    float damage = 0.0f;
    std::uint32_t age = 0;  // ticks since it was fired
};

enum class GameState {
    Playing,
    Defeat,
};

enum class PlacementResult {
    Placed,
    BlockedTile,    // wall, existing tower, spawn or goal
    WouldSealPath,  // legal tile, but it would wall the route off entirely
    NotEnoughGold,
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

// The entire game simulation. Pure C++ with no platform or graphics
// dependency: it is driven by step() and observed through the accessors, so it
// runs identically inside the renderer-less test suite and on every platform.
class World {
public:
    World(int width = config::kGridWidth, int height = config::kGridHeight,
          std::uint64_t seed = 12345);

    // Advances the simulation by exactly one tick.
    //
    // There is deliberately no dt parameter. The simulation's clock is tick_,
    // and the fixed rate is a property of the world rather than something the
    // caller supplies -- a dt argument that nothing enforces is a standing
    // invitation to pass a frame delta during a debugging session and quietly
    // destroy reproducibility. config::kFixedDt still exists for the spatial
    // math inside a tick; it is not a knob.
    void step();

    PlacementResult placeTower(Cell cell);
    bool canAfford() const { return gold_ >= config::kTowerCost; }

    // --- observation (rendering, UI, tests) ---
    const Grid& grid() const { return grid_; }
    const FlowField& flowField() const { return flow_; }
    // Views, not containers. Callers only ever iterate, subscript and size
    // these, so naming the underlying storage in the signature would have made
    // Phase 2's pool migration a breaking change at every call site. See Span.h.
    Span<const Enemy> enemies() const { return enemies_.items(); }
    Span<const Tower> towers() const { return towers_.items(); }
    Span<const Projectile> projectiles() const { return projectiles_.items(); }

    // Whether a tower may be placed here, ignoring cost. Memoized on the hover
    // cell and a grid version counter, so the placement preview can ask this
    // every frame without running a BFS or allocating.
    bool canPlaceAt(Cell cell) const;

    Cell spawn() const { return spawn_; }
    Cell goal() const { return goal_; }
    int gold() const { return gold_; }
    int lives() const { return lives_; }
    int waveNumber() const { return wave_; }
    std::uint64_t tick() const { return tick_; }
    // Ticks converted for display. The simulation itself never sees this.
    float timeUntilNextWave() const { return static_cast<float>(waveTimer_) * config::kFixedDt; }
    bool waveInProgress() const { return enemiesLeftToSpawn_ > 0 || !enemies_.empty(); }
    GameState state() const { return state_; }
    int enemiesLeaked() const { return leaked_; }
    int enemiesKilled() const { return killed_; }

    // The world's canonical byte stream: every piece of state that a
    // reproducible run depends on, in a fixed order, written as explicit
    // little-endian integers.
    //
    // It lives here rather than in Hash.h on purpose. A serializer kept in
    // another file is a serializer that gets a field added behind its back, and
    // a field missing from this function is a divergence the hash cannot see.
    // Adding a member to World means adding a line here.
    //
    // Sink supplies u8/u32/u64/i32/f32. Templated so the same field order
    // serves the determinism hash now and the Phase 7 save writer later -- two
    // separate implementations of one format would drift.
    //
    // Field order IS the format. Appending is safe; reordering or removing
    // changes every hash, which is what simVersion will exist to notice.
    template <typename Sink>
    void serialize(Sink& out) const {
        out.u64(tick_);
        out.i32(gold_);
        out.i32(lives_);
        out.i32(wave_);
        out.i32(leaked_);
        out.i32(killed_);
        out.u32(static_cast<std::uint32_t>(state_));
        out.u32(enemies_.nextSequence());
        out.u32(towers_.nextSequence());
        out.u32(projectiles_.nextSequence());

        // Derived from the grid dimensions today, so hashing the dimensions
        // already covers them. Written explicitly anyway: the moment a level
        // loader sets a spawn or goal that is not a function of the size, this
        // stops being true, and goal_ drives the flow field that every enemy
        // steers by.
        out.i32(spawn_.x);
        out.i32(spawn_.y);
        out.i32(goal_.x);
        out.i32(goal_.y);

        // Wave director. Invisible to the player and decisive for the run.
        out.i32(enemiesLeftToSpawn_);
        out.u32(spawnTimer_);
        out.u32(waveTimer_);
        out.f32(waveHealthScale_);
        out.f32(waveSpeedScale_);

        out.u32(static_cast<std::uint32_t>(enemies_.size()));
        for (const Enemy* p : detail::byId<Enemy>(enemies())) {
            const Enemy& enemy = *p;
            out.u32(enemy.id);
            out.f32(enemy.position.x);
            out.f32(enemy.position.y);
            out.f32(enemy.health);
            out.f32(enemy.maxHealth);
            out.f32(enemy.speed);
        }

        out.u32(static_cast<std::uint32_t>(towers_.size()));
        for (const Tower* p : detail::byId<Tower>(towers())) {
            const Tower& tower = *p;
            out.u32(tower.id);
            out.i32(tower.cell.x);
            out.i32(tower.cell.y);
            out.f32(tower.range);
            out.f32(tower.damage);
            out.u32(tower.cooldown);
            // aim is rendering-only, but it is derived from simulation state
            // and costs two words to cover. A field left out of the hash is a
            // field that can diverge silently.
            out.f32(tower.aim.x);
            out.f32(tower.aim.y);
        }

        out.u32(static_cast<std::uint32_t>(projectiles_.size()));
        for (const Projectile* p : detail::byId<Projectile>(projectiles())) {
            const Projectile& shot = *p;
            out.f32(shot.position.x);
            out.f32(shot.position.y);
            out.f32(shot.velocity.x);
            out.f32(shot.velocity.y);
            out.u32(shot.id);
            out.u32(shot.targetId);
            out.f32(shot.damage);
            out.u32(shot.age);
        }

        // The grid is state, not level constants: towers change it as they are
        // built, and the flow field is derived from it.
        out.i32(grid_.width());
        out.i32(grid_.height());
        for (int y = 0; y < grid_.height(); ++y) {
            for (int x = 0; x < grid_.width(); ++x) {
                out.u8(static_cast<std::uint8_t>(grid_.at({x, y})));
            }
        }

        // flow_ is deliberately absent. It is a pure function of (grid_, goal_),
        // both of which are written above, and it is rebuilt on the only path
        // that mutates the grid. So it cannot diverge independently -- this is
        // the one exception to the rule stated at the top of this function.
        //
        // It does matter for Phase 7: a save writer using this stream will not
        // persist the distance cache, so the loader must call
        // flow_.build(grid_, goal_) after restoring the grid, or every enemy
        // stands still.

        // Every stream's position, including the ones nothing has drawn from
        // yet. Two worlds that look identical but sit at different points in a
        // stream are different runs -- they diverge on the next draw.
        for (std::size_t i = 0; i < rng_.size(); ++i) {
            out.u64(rng_.at(i).state());
        }
    }

    // 64-bit fingerprint of the entire simulation state. Two runs that agree on
    // this at every tick are the same run; a tolerance-based comparison can
    // only ever say they are similar.
    std::uint64_t checksum() const;

private:
    void stepWaveDirector();
    void stepEnemies();
    void stepTowers();
    void stepProjectiles();

    void spawnEnemy();
    void startNextWave();
    EnemyId findTarget(const Tower& tower) const;

    Grid grid_;
    FlowField flow_;
    Cell spawn_{};
    Cell goal_{};

    // Each pool owns its own monotonic id sequence, rather than the three of
    // them drawing from one shared counter. Shared meant that adding ids to
    // projectiles renumbered every enemy and tower, because they came out of the
    // same sequence -- a format change in one entity type silently rewriting the
    // identities of the others.
    Pool<Enemy, EnemyTag> enemies_;
    Pool<Tower, TowerTag> towers_;
    Pool<Projectile, ProjectileTag> projectiles_;

    // Rebuilt from enemies_ once per tick, before the towers pick targets.
    SpatialIndex<Enemy> enemyIndex_;

    // Scratch, reserved once and reused. step() must not allocate.
    std::vector<EnemyId> enemyRemovals_;
    std::vector<ProjectileId> projectileRemovals_;

    // Bumped whenever a tower changes the grid, which is the only thing that can
    // change a placement answer. The preview memo is keyed on it.
    std::uint32_t gridVersion_ = 0;
    mutable PlacementQuery placement_;
    mutable Cell placementCell_{-1, -1};
    mutable std::uint32_t placementVersion_ = 0;
    mutable bool placementResult_ = false;
    mutable bool placementCached_ = false;

    RngStreams rng_;

    // The simulation clock. Everything time-dependent counts ticks, not
    // seconds, so a run is reproducible by construction rather than by
    // convention.
    std::uint64_t tick_ = 0;

    int gold_ = config::kStartingGold;
    int lives_ = config::kStartingLives;
    int wave_ = 0;
    // Compounding difficulty for the wave in progress, recomputed only when a
    // wave starts. See startNextWave().
    float waveHealthScale_ = 1.0f;
    float waveSpeedScale_ = 1.0f;
    int enemiesLeftToSpawn_ = 0;
    std::uint32_t spawnTimer_ = 0;
    std::uint32_t waveTimer_ = config::kTimeBetweenWavesTicks;
    int leaked_ = 0;
    int killed_ = 0;
    GameState state_ = GameState::Playing;
};

}  // namespace ktd
