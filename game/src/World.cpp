#include "game/World.h"

#include "game/Hash.h"

namespace ktd {

World::World(const content::ContentDb& content, std::uint64_t seed)
    : content_(&content),
      contentFingerprint_(content.fingerprint()),
      grid_(content.level().width, content.level().height),
      rng_(seed) {
    // step() must not allocate, so every per-tick buffer gets its capacity here.
    // The numbers are headroom for a busy late wave, not hard limits: exceeding
    // one costs a single reallocation, not a bug.
    enemies_.reserve(512);
    towers_.reserve(128);
    projectiles_.reserve(512);
    enemyRemovals_.reserve(64);
    projectileRemovals_.reserve(128);

    // Level geometry, the route and the starting economy all arrive as data.
    // This used to be a hardcoded pair of wall columns with a comment promising
    // a loader; the loader is content::load(), and it has already validated
    // that a route from spawn to goal exists.
    const content::LevelDef& level = content.level();
    spawn_ = level.spawn;
    goal_ = level.goal;
    for (int y = 0; y < level.height; ++y) {
        for (int x = 0; x < level.width; ++x) {
            grid_.set({x, y}, level.at({x, y}));
        }
    }
    gold_ = level.startingGold;
    lives_ = level.startingLives;
    waveTimer_ = content.waves().gapTicks;

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

// How many enemies wave `wave` sends. Authored waves say so exactly; past the
// end of the authored list the endless rule takes over and grows from there.
int World::enemiesInWave(int wave) const {
    const content::WaveRules& rules = content_->waves();
    const int authored = static_cast<int>(rules.waves.size());
    if (wave <= authored) {
        return rules.waves[static_cast<std::size_t>(wave - 1)].totalEnemies();
    }
    const int beyond = wave - authored;  // 1 for the first endless wave
    return rules.endless.firstCount + (beyond - 1) * rules.endless.addedPerWave;
}

// The type of the nth enemy of the current wave, counting from zero.
//
// Resolved per spawn from an index rather than by materialising the wave into a
// queue, because a queue is a per-wave allocation and a member the serializer
// would have to write. The composition is at most a handful of groups.
content::EnemyDefId World::spawnTypeAt(int indexInWave) const {
    const content::WaveRules& rules = content_->waves();
    const int authored = static_cast<int>(rules.waves.size());
    if (wave_ > authored) {
        return rules.endless.enemy;
    }

    const content::WaveDef& def = rules.waves[static_cast<std::size_t>(wave_ - 1)];
    int consumed = 0;
    for (std::size_t group = 0; group < def.enemies.size(); ++group) {
        consumed += def.counts[group];
        if (indexInWave < consumed) {
            return def.enemies[group];
        }
    }
    return content::kNoDef;
}

void World::startNextWave() {
    ++wave_;
    enemiesLeftToSpawn_ = enemiesInWave(wave_);
    spawnedInWave_ = 0;
    spawnTimer_ = 0;

    // Computed once here rather than per spawn. Two reasons, and only one of
    // them is speed: recomputing a compounding scale for every enemy means the
    // wave's difficulty is derived in the hot path from a value that must never
    // vary within the wave. Deriving it once at the wave boundary makes that
    // structural instead of merely true.
    //
    // An authored wave is not scaled at all -- what the designer typed is what
    // arrives, which is the whole reason to write one by hand. Only the endless
    // progression compounds, and it counts from the first endless wave rather
    // than from wave one, so authoring an opening wave does not silently shift
    // the difficulty curve behind it.
    const content::WaveRules& rules = content_->waves();
    const int authored = static_cast<int>(rules.waves.size());
    if (wave_ <= authored) {
        waveHealthScale_ = 1.0f;
        waveSpeedScale_ = 1.0f;
    } else {
        const int beyond = wave_ - authored;
        waveHealthScale_ = math::powi(rules.endless.healthGrowth, beyond - 1);
        waveSpeedScale_ = math::powi(rules.endless.speedGrowth, beyond - 1);
    }
}

void World::stepWaveDirector() {
    if (enemiesLeftToSpawn_ > 0) {
        if (spawnTimer_ > 0) {
            --spawnTimer_;
        }
        if (spawnTimer_ == 0) {
            spawnEnemy();
            --enemiesLeftToSpawn_;
            spawnTimer_ = content_->waves().spawnIntervalTicks;
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
        waveTimer_ = content_->waves().gapTicks;
        startNextWave();
    }
}

void World::spawnEnemy() {
    const content::EnemyDefId type = spawnTypeAt(spawnedInWave_);
    ++spawnedInWave_;
    if (type == content::kNoDef) {
        return;  // an empty wave; the counter still advances so timing holds
    }
    const content::EnemyDef& def = content_->enemy(type);

    Enemy enemy;
    enemy.type = type;
    enemy.position = Grid::centerOf(spawn_);
    // Spread arrivals across the tile so a wave does not render as one blob.
    enemy.position.y += rng_[RngStream::SpawnJitter].range(-0.25f, 0.25f);
    enemy.maxHealth = def.health * waveHealthScale_;
    enemy.health = enemy.maxHealth;
    enemy.speed = def.speed * waveSpeedScale_;

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
    const std::uint32_t slot = selectTarget<Enemy>(enemies_.items(), enemyIndex_, flow_, tower.cell,
                                                   content_->tower(tower.type).range);
    return slot == kNoTarget ? EnemyId{} : enemies_.handleAt(slot);
}

void World::stepTowers() {
    for (Tower& tower : towers_.items()) {
        // Every number this tower fights with is read here, on the tick it
        // fires. That is what makes a hot reload reach towers already standing
        // on the board rather than only the ones built after it.
        const content::TowerDef& def = content_->tower(tower.type);

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
        tower.cooldown = def.reloadTicks;

        Projectile shot;
        shot.position = origin;
        shot.velocity = tower.aim * content_->projectile(def.projectile).speed;
        shot.target = targetId;
        shot.targetId = target->id;
        shot.type = def.projectile;
        shot.damage = def.damage;
        projectiles_.create(shot);
    }
}

// --- projectiles -----------------------------------------------------------

void World::stepProjectiles() {
    enemyRemovals_.clear();
    projectileRemovals_.clear();

    for (std::size_t i = 0; i < projectiles_.size(); ++i) {
        Projectile& shot = projectiles_.items()[i];
        const content::ProjectileDef& def = content_->projectile(shot.type);
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
            shot.velocity = normalized(target->position - shot.position) * def.speed;
        }
        shot.position = shot.position + shot.velocity * config::kFixedDt;

        if (target) {
            const float hitRadius = def.hitRadius + content_->enemy(target->type).radius;
            if (lengthSq(target->position - shot.position) <= hitRadius * hitRadius) {
                target->health -= shot.damage;
                shot.age = def.lifetimeTicks;  // consumed on impact
                if (target->health <= 0.0f) {
                    enemyRemovals_.push_back(shot.target);
                }
            }
        }

        if (shot.age >= def.lifetimeTicks) {
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
        // The bounty has to be read before the enemy is destroyed, and get()
        // returns null for a handle a previous iteration already consumed --
        // which is the same generation check that keeps the payout from
        // doubling, now doing double duty.
        const Enemy* victim = enemies_.get(id);
        const int bounty = victim != nullptr ? content_->enemy(victim->type).bounty : 0;
        if (enemies_.destroy(id)) {
            gold_ += bounty;
            ++killed_;
        }
    }
}

// --- building --------------------------------------------------------------

bool World::canAfford(content::TowerDefId type) const {
    return type < content_->towers().size() && gold_ >= content_->tower(type).cost;
}

PlacementResult World::placeTower(Cell cell, content::TowerDefId type) {
    if (type >= content_->towers().size()) {
        return PlacementResult::UnknownTowerType;
    }
    const content::TowerDef& def = content_->tower(type);
    if (gold_ < def.cost) {
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
    tower.type = type;
    towers_.create(tower);

    gold_ -= def.cost;
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
