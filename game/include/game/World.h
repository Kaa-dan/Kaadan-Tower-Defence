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
