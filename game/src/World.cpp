#include "game/World.h"

#include <algorithm>
#include <limits>

namespace ktd {

World::World(int width, int height, std::uint64_t seed) : grid_(width, height), rng_(seed) {
    spawn_ = {0, height / 2};
    goal_ = {width - 1, height / 2};

    // A few walls so the default level is not an empty box. Purely cosmetic
    // for now; a real level loader replaces this.
    for (int y = 3; y < height - 3; ++y) {
        if (y == height / 2) continue;  // leave the corridor open
        grid_.set({width / 3, y}, Tile::Wall);
        grid_.set({2 * width / 3, y}, Tile::Wall);
    }

    flow_.build(grid_, goal_);
}

void World::step(float dt) {
    if (state_ != GameState::Playing) {
        return;
    }

    stepWaveDirector(dt);
    stepEnemies(dt);
    stepTowers(dt);
    stepProjectiles(dt);

    if (lives_ <= 0) {
        lives_ = 0;
        state_ = GameState::Defeat;
    }
}

// --- waves -----------------------------------------------------------------

void World::startNextWave() {
    ++wave_;
    enemiesLeftToSpawn_ = config::kEnemiesInFirstWave + (wave_ - 1) * config::kEnemiesAddedPerWave;
    spawnTimer_ = 0.0f;

    // Computed once here rather than per spawn. Two reasons, and only one of
    // them is speed: recomputing a compounding scale for every enemy means the
    // wave's difficulty is derived in the hot path from a value that must never
    // vary within the wave. Deriving it once at the wave boundary makes that
    // structural instead of merely true.
    waveHealthScale_ = math::powi(config::kEnemyHealthGrowth, wave_ - 1);
    waveSpeedScale_ = math::powi(config::kEnemySpeedGrowth, wave_ - 1);
}

void World::stepWaveDirector(float dt) {
    if (enemiesLeftToSpawn_ > 0) {
        spawnTimer_ -= dt;
        if (spawnTimer_ <= 0.0f) {
            spawnEnemy();
            --enemiesLeftToSpawn_;
            spawnTimer_ = config::kSpawnInterval;
        }
        return;
    }

    // Only start counting down to the next wave once the field is clear, so
    // the player always gets their full breather.
    if (!enemies_.empty()) {
        return;
    }

    waveTimer_ -= dt;
    if (waveTimer_ <= 0.0f) {
        waveTimer_ = config::kTimeBetweenWaves;
        startNextWave();
    }
}

void World::spawnEnemy() {
    Enemy enemy;
    enemy.id = nextId_++;
    enemy.position = Grid::centerOf(spawn_);
    // Spread arrivals across the tile so a wave does not render as one blob.
    enemy.position.y += rng_[RngStream::SpawnJitter].range(-0.25f, 0.25f);
    enemy.maxHealth = config::kEnemyBaseHealth * waveHealthScale_;
    enemy.health = enemy.maxHealth;
    enemy.speed = config::kEnemyBaseSpeed * waveSpeedScale_;

    enemies_.push_back(enemy);
}

// --- enemies ---------------------------------------------------------------

void World::stepEnemies(float dt) {
    for (Enemy& enemy : enemies_) {
        const Cell current = Grid::cellOf(enemy.position);

        // Steer toward the centre of the next tile on the flow field. Aiming
        // at tile centres keeps enemies off the corners of towers.
        const Cell nextCell = flow_.next(current);
        const Vec2 target = Grid::centerOf(nextCell == current ? goal_ : nextCell);

        const Vec2 delta = target - enemy.position;
        const float distance = length(delta);
        const float travel = enemy.speed * dt;

        if (distance <= travel) {
            enemy.position = target;
        } else {
            enemy.position = enemy.position + normalized(delta) * travel;
        }
    }

    // Enemies that reached the goal cost a life and leave the field.
    const Vec2 goalCenter = Grid::centerOf(goal_);
    const auto reachedGoal = [&](const Enemy& e) {
        return lengthSq(e.position - goalCenter) <= config::kEnemyRadius * config::kEnemyRadius;
    };

    for (const Enemy& enemy : enemies_) {
        if (reachedGoal(enemy)) {
            --lives_;
            ++leaked_;
        }
    }
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), reachedGoal), enemies_.end());
}

// --- towers ----------------------------------------------------------------

const Enemy* World::findTarget(const Tower& tower) const {
    // "Furthest along the path" targeting: of everything in range, shoot the
    // enemy closest to the goal. This is the standard TD default because it
    // prioritises the leaks that actually cost the player a life.
    const Vec2 origin = Grid::centerOf(tower.cell);
    const float rangeSq = tower.range * tower.range;

    const Enemy* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();

    for (const Enemy& enemy : enemies_) {
        if (lengthSq(enemy.position - origin) > rangeSq) {
            continue;
        }
        const int toGoal = flow_.distance(Grid::cellOf(enemy.position));
        if (toGoal >= 0 && toGoal < bestDistance) {
            bestDistance = toGoal;
            best = &enemy;
        }
    }
    return best;
}

void World::stepTowers(float dt) {
    const float reloadTime = 1.0f / config::kTowerShotsPerSecond;

    for (Tower& tower : towers_) {
        tower.cooldown -= dt;

        const Enemy* target = findTarget(tower);
        if (target == nullptr) {
            continue;
        }

        const Vec2 origin = Grid::centerOf(tower.cell);
        tower.aim = normalized(target->position - origin);

        if (tower.cooldown > 0.0f) {
            continue;
        }
        tower.cooldown = reloadTime;

        Projectile shot;
        shot.position = origin;
        shot.velocity = tower.aim * config::kProjectileSpeed;
        shot.targetId = target->id;
        shot.damage = tower.damage;
        projectiles_.push_back(shot);
    }
}

// --- projectiles -----------------------------------------------------------

Enemy* World::findEnemy(EntityId id) {
    for (Enemy& enemy : enemies_) {
        if (enemy.id == id) {
            return &enemy;
        }
    }
    return nullptr;
}

void World::stepProjectiles(float dt) {
    std::vector<EntityId> killedThisStep;

    for (Projectile& shot : projectiles_) {
        shot.age += dt;

        // Home in while the target lives; otherwise fly straight and expire.
        if (const Enemy* target = findEnemy(shot.targetId)) {
            shot.velocity = normalized(target->position - shot.position) * config::kProjectileSpeed;
        }
        shot.position = shot.position + shot.velocity * dt;

        Enemy* target = findEnemy(shot.targetId);
        if (target == nullptr) {
            continue;
        }

        const float hitRadius = config::kProjectileHitRadius + config::kEnemyRadius;
        if (lengthSq(target->position - shot.position) <= hitRadius * hitRadius) {
            target->health -= shot.damage;
            shot.age = config::kProjectileLifetime;  // consumed on impact
            if (target->health <= 0.0f) {
                killedThisStep.push_back(target->id);
            }
        }
    }

    projectiles_.erase(
        std::remove_if(projectiles_.begin(), projectiles_.end(),
                       [](const Projectile& p) { return p.age >= config::kProjectileLifetime; }),
        projectiles_.end());

    for (const EntityId id : killedThisStep) {
        const auto it = std::find_if(enemies_.begin(), enemies_.end(),
                                     [id](const Enemy& e) { return e.id == id; });
        if (it != enemies_.end()) {
            enemies_.erase(it);
            gold_ += config::kGoldPerKill;
            ++killed_;
        }
    }
}

// --- building --------------------------------------------------------------

PlacementResult World::placeTower(Cell cell) {
    if (gold_ < config::kTowerCost) {
        return PlacementResult::NotEnoughGold;
    }
    if (!grid_.inBounds(cell) || grid_.at(cell) != Tile::Open || cell == spawn_ || cell == goal_) {
        return PlacementResult::BlockedTile;
    }
    if (!canPlaceTower(grid_, cell, spawn_, goal_)) {
        return PlacementResult::WouldSealPath;
    }

    // Refuse to strand an enemy that is standing on the tile right now.
    for (const Enemy& enemy : enemies_) {
        if (Grid::cellOf(enemy.position) == cell) {
            return PlacementResult::BlockedTile;
        }
    }

    grid_.set(cell, Tile::Tower);
    flow_.build(grid_, goal_);

    Tower tower;
    tower.id = nextId_++;
    tower.cell = cell;
    towers_.push_back(tower);

    gold_ -= config::kTowerCost;
    return PlacementResult::Placed;
}

}  // namespace ktd
