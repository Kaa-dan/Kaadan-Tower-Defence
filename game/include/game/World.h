#pragma once

#include "game/Config.h"
#include "game/Grid.h"
#include "game/Path.h"
#include "game/Rng.h"
#include "game/Types.h"

#include <vector>

namespace ktd {

using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidId = 0;

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
    Vec2 position{};
    Vec2 velocity{};
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
    const std::vector<Enemy>& enemies() const { return enemies_; }
    const std::vector<Tower>& towers() const { return towers_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }

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
        out.u32(nextId_);

        // Wave director. Invisible to the player and decisive for the run.
        out.i32(enemiesLeftToSpawn_);
        out.u32(spawnTimer_);
        out.u32(waveTimer_);
        out.f32(waveHealthScale_);
        out.f32(waveSpeedScale_);

        out.u32(static_cast<std::uint32_t>(enemies_.size()));
        for (const Enemy& enemy : enemies_) {
            out.u32(enemy.id);
            out.f32(enemy.position.x);
            out.f32(enemy.position.y);
            out.f32(enemy.health);
            out.f32(enemy.maxHealth);
            out.f32(enemy.speed);
        }

        out.u32(static_cast<std::uint32_t>(towers_.size()));
        for (const Tower& tower : towers_) {
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
        for (const Projectile& shot : projectiles_) {
            out.f32(shot.position.x);
            out.f32(shot.position.y);
            out.f32(shot.velocity.x);
            out.f32(shot.velocity.y);
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
    Enemy* findEnemy(EntityId id);
    const Enemy* findTarget(const Tower& tower) const;

    Grid grid_;
    FlowField flow_;
    Cell spawn_{};
    Cell goal_{};

    std::vector<Enemy> enemies_;
    std::vector<Tower> towers_;
    std::vector<Projectile> projectiles_;

    RngStreams rng_;
    EntityId nextId_ = 1;

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
