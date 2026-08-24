#include "game/World.h"

#include "game/Hash.h"

namespace ktd {

World::World(int width, int height, std::uint64_t seed) : grid_(width, height), rng_(seed) {
    // step() must not allocate, so every per-tick buffer gets its capacity here.
    // The numbers are headroom for a busy late wave, not hard limits: exceeding
    // one costs a single reallocation, not a bug.
    enemies_.reserve(512);
    towers_.reserve(128);
    projectiles_.reserve(512);
    enemyRemovals_.reserve(64);
    projectileRemovals_.reserve(128);

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

std::uint64_t World::checksum() const { return hashWorld(*this); }

void World::step() {
    if (state_ != GameState::Playing) {
        return;
    }

    ++tick_;

    stepWaveDirector();
    stepEnemies();
    // Rebuilt after the enemies move and before the towers look for them, so a
    // tower never targets an enemy at last tick's position.
    enemyIndex_.rebuild(grid_.width(), grid_.height(), enemies_.items());
    stepTowers();
    stepProjectiles();

    if (lives_ <= 0) {
        lives_ = 0;
        state_ = GameState::Defeat;
    }
}

// --- waves -----------------------------------------------------------------

void World::startNextWave() {
    ++wave_;
    enemiesLeftToSpawn_ = config::kEnemiesInFirstWave + (wave_ - 1) * config::kEnemiesAddedPerWave;
    spawnTimer_ = 0;

    // Computed once here rather than per spawn. Two reasons, and only one of
    // them is speed: recomputing a compounding scale for every enemy means the
    // wave's difficulty is derived in the hot path from a value that must never
    // vary within the wave. Deriving it once at the wave boundary makes that
    // structural instead of merely true.
    waveHealthScale_ = math::powi(config::kEnemyHealthGrowth, wave_ - 1);
    waveSpeedScale_ = math::powi(config::kEnemySpeedGrowth, wave_ - 1);
}

void World::stepWaveDirector() {
    if (enemiesLeftToSpawn_ > 0) {
        if (spawnTimer_ > 0) {
            --spawnTimer_;
        }
        if (spawnTimer_ == 0) {
            spawnEnemy();
            --enemiesLeftToSpawn_;
            spawnTimer_ = config::kSpawnIntervalTicks;
        }
        return;
    }

    // Only start counting down to the next wave once the field is clear, so
    // the player always gets their full breather.
    if (!enemies_.empty()) {
        return;
    }

    if (waveTimer_ > 0) {
        --waveTimer_;
    }
    if (waveTimer_ == 0) {
        waveTimer_ = config::kTimeBetweenWavesTicks;
        startNextWave();
    }
}

void World::spawnEnemy() {
    Enemy enemy;
    enemy.position = Grid::centerOf(spawn_);
    // Spread arrivals across the tile so a wave does not render as one blob.
    enemy.position.y += rng_[RngStream::SpawnJitter].range(-0.25f, 0.25f);
    enemy.maxHealth = config::kEnemyBaseHealth * waveHealthScale_;
    enemy.health = enemy.maxHealth;
    enemy.speed = config::kEnemyBaseSpeed * waveSpeedScale_;

    // The pool stamps enemy.id from its own sequence.
    enemies_.create(enemy);
}

// --- enemies ---------------------------------------------------------------

void World::stepEnemies() {
    for (Enemy& enemy : enemies_.items()) {
        const Cell current = Grid::cellOf(enemy.position);

        // Steer toward the centre of the next tile on the flow field. Aiming
        // at tile centres keeps enemies off the corners of towers.
        const Cell nextCell = flow_.next(current);
        const Vec2 target = Grid::centerOf(nextCell == current ? goal_ : nextCell);

        const Vec2 delta = target - enemy.position;
        const float distance = length(delta);
        const float travel = enemy.speed * config::kFixedDt;

        if (distance <= travel) {
            enemy.position = target;
        } else {
            enemy.position = enemy.position + normalized(delta) * travel;
        }
    }

    // Enemies that reached the goal cost a life and leave the field.
    //
    // A cell test, not a radius test. The old version asked whether the enemy
    // was within kEnemyRadius of the goal's centre, which a fast enemy could
    // step straight over: it would pass the goal and keep walking, never
    // registering a leak, and the player would lose no life for an enemy that
    // plainly got through (known-wrong #6).
    //
    // The cell test cannot be tunnelled, and the reason is the movement code
    // above rather than anything here. An enemy always steers at the centre of
    // an adjacent tile and clamps exactly onto it when its travel would
    // overshoot, so it advances at most one tile per tick and always lands on
    // centres. There is no step that can jump the goal cell.
    const auto reachedGoal = [&](const Enemy& e) { return Grid::cellOf(e.position) == goal_; };

    // Collected first, destroyed second. Destroying inside the loop would
    // swap-and-pop an entity the loop has not visited yet into a slot it has
    // already passed, and that enemy would leak without costing a life.
    enemyRemovals_.clear();
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        if (reachedGoal(enemies_.items()[i])) {
            enemyRemovals_.push_back(enemies_.handleAt(i));
            --lives_;
            ++leaked_;
        }
    }
    for (const EnemyId id : enemyRemovals_) {
        enemies_.destroy(id);
    }
}

// --- towers ----------------------------------------------------------------

EnemyId World::findTarget(const Tower& tower) const {
    const std::uint32_t slot =
        selectTarget<Enemy>(enemies_.items(), enemyIndex_, flow_, tower.cell, tower.range);
    return slot == kNoTarget ? EnemyId{} : enemies_.handleAt(slot);
}

void World::stepTowers() {
    for (Tower& tower : towers_.items()) {
        if (tower.cooldown > 0) {
            --tower.cooldown;
        }

        const EnemyId targetId = findTarget(tower);
        const Enemy* target = enemies_.get(targetId);
        if (target == nullptr) {
            continue;
        }

        const Vec2 origin = Grid::centerOf(tower.cell);
        tower.aim = normalized(target->position - origin);

        if (tower.cooldown > 0) {
            continue;
        }
        tower.cooldown = config::kTowerReloadTicks;

        Projectile shot;
        shot.position = origin;
        shot.velocity = tower.aim * config::kProjectileSpeed;
        shot.target = targetId;
        shot.targetId = target->id;
        shot.damage = tower.damage;
        projectiles_.create(shot);
    }
}

// --- projectiles -----------------------------------------------------------

void World::stepProjectiles() {
    enemyRemovals_.clear();
    projectileRemovals_.clear();

    for (std::size_t i = 0; i < projectiles_.size(); ++i) {
        Projectile& shot = projectiles_.items()[i];
        ++shot.age;

        // One O(1) handle lookup, where this used to run two full linear scans
        // over every enemy for every projectile -- and the second scan asked the
        // same question as the first (known-wrong #3). A dead target resolves to
        // nullptr through the generation check rather than needing a search to
        // discover it is gone.
        // borrow() rather than get(): a Ref re-checks the pool's mutation epoch
        // on every dereference. This is the exact loop that used to hold a raw
        // pointer into a std::vector, and the exact place a future change --
        // area damage that kills on impact, say -- would be tempted to destroy
        // an enemy mid-iteration and leave this pointer naming whatever
        // swap-and-pop moved into its place. That now trips an assert in a debug
        // build instead of silently damaging the wrong enemy in a release one.
        auto target = enemies_.borrow(shot.target);

        // Home in while the target lives; otherwise fly straight and expire.
        if (target) {
            shot.velocity = normalized(target->position - shot.position) * config::kProjectileSpeed;
        }
        shot.position = shot.position + shot.velocity * config::kFixedDt;

        if (target) {
            const float hitRadius = config::kProjectileHitRadius + config::kEnemyRadius;
            if (lengthSq(target->position - shot.position) <= hitRadius * hitRadius) {
                target->health -= shot.damage;
                shot.age = config::kProjectileLifetimeTicks;  // consumed on impact
                if (target->health <= 0.0f) {
                    enemyRemovals_.push_back(shot.target);
                }
            }
        }

        if (shot.age >= config::kProjectileLifetimeTicks) {
            projectileRemovals_.push_back(projectiles_.handleAt(i));
        }
    }

    for (const ProjectileId id : projectileRemovals_) {
        projectiles_.destroy(id);
    }

    // Two shots can land on the same enemy in one tick, so the same handle can
    // appear twice. destroy() returns false the second time -- the generation
    // has already moved on -- which is what keeps the payout from doubling.
    // Relying on that rather than de-duplicating first is the point of a
    // generation handle: the stale case is handled, not avoided.
    for (const EnemyId id : enemyRemovals_) {
        if (enemies_.destroy(id)) {
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
    for (const Enemy& enemy : enemies_.items()) {
        if (Grid::cellOf(enemy.position) == cell) {
            return PlacementResult::BlockedTile;
        }
    }

    grid_.set(cell, Tile::Tower);
    ++gridVersion_;  // invalidates the placement preview memo
    flow_.build(grid_, goal_);

    Tower tower;
    tower.cell = cell;
    towers_.create(tower);

    gold_ -= config::kTowerCost;
    return PlacementResult::Placed;
}

bool World::canPlaceAt(Cell cell) const {
    // The placement preview asks this every frame for a cell the mouse usually
    // has not left, and the answer can only change when a tower changes the
    // grid. Memoizing on (cell, gridVersion_) turns the common case into two
    // integer compares; the miss runs one BFS over reused buffers, where the old
    // code copied the whole Grid and allocated a std::deque every single frame
    // (known-wrong #4).
    if (placementCached_ && placementVersion_ == gridVersion_ && placementCell_ == cell) {
        return placementResult_;
    }

    bool result = false;
    if (grid_.inBounds(cell) && grid_.at(cell) == Tile::Open && cell != spawn_ && cell != goal_) {
        result = placement_.routeSurvives(grid_, cell, spawn_, goal_);
    }

    placementCached_ = true;
    placementCell_ = cell;
    placementVersion_ = gridVersion_;
    placementResult_ = result;
    return result;
}

}  // namespace ktd
