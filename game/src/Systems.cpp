#include "game/Systems.h"

#include "game/Config.h"
#include "game/Damage.h"
#include "game/Status.h"
#include "game/Targeting.h"

namespace ktd::sys {

namespace {

Event makeEvent(EventType type, EntityId subject, Vec2 position) {
    Event event;
    event.type = type;
    event.subject = subject;
    event.position = position;
    return event;
}

// Which enemy type the nth spawn of the current wave is, counting from zero.
//
// Resolved per spawn from an index rather than by materialising the wave into a
// queue, because a queue is a per-wave allocation and a member the serializer
// would have to write. The composition is at most a handful of groups.
content::EnemyDefId spawnTypeAt(const content::ContentDb& content, int wave, int indexInWave) {
    const content::WaveRules& rules = content.waves();
    const int authored = static_cast<int>(rules.waves.size());
    if (wave > authored) {
        return rules.endless.enemy;
    }

    const content::WaveDef& def = rules.waves[static_cast<std::size_t>(wave - 1)];
    int consumed = 0;
    for (std::size_t group = 0; group < def.enemies.size(); ++group) {
        consumed += def.counts[group];
        if (indexInWave < consumed) {
            return def.enemies[group];
        }
    }
    return content::kNoDef;
}

// True when the content says the run can be won: authored waves exist, and
// nothing takes over once they run out.
//
// A level with an endless rule is never winnable, which is what the shipped
// content is today -- so adding victory changed no existing behaviour. Making
// it a property of the data rather than a flag means a campaign level and an
// endless level are the same code and a different file.
bool isWinnable(const content::ContentDb& content) {
    const content::WaveRules& rules = content.waves();
    if (rules.waves.empty()) {
        return false;
    }
    return rules.endless.enemy == content::kNoDef || rules.endless.firstCount <= 0;
}

void startNextWave(SimContext& ctx) {
    WorldState& s = ctx.state;
    ++s.wave;
    s.enemiesLeftToSpawn = enemiesInWave(ctx.content, s.wave);
    s.spawnedInWave = 0;
    s.spawnTimer = 0;

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
    const content::WaveRules& rules = ctx.content.waves();
    const int authored = static_cast<int>(rules.waves.size());
    if (s.wave <= authored) {
        s.waveHealthScale = 1.0f;
        s.waveSpeedScale = 1.0f;
    } else {
        const int beyond = s.wave - authored;
        s.waveHealthScale = math::powi(rules.endless.healthGrowth, beyond - 1);
        s.waveSpeedScale = math::powi(rules.endless.speedGrowth, beyond - 1);
    }

    Event event = makeEvent(EventType::WaveStarted, kInvalidId, Grid::centerOf(s.spawn));
    event.detail = static_cast<std::uint32_t>(s.wave);
    event.value = static_cast<float>(s.enemiesLeftToSpawn);
    ctx.events.push(event);
}

void spawnEnemy(SimContext& ctx) {
    WorldState& s = ctx.state;
    const content::EnemyDefId type = spawnTypeAt(ctx.content, s.wave, s.spawnedInWave);
    ++s.spawnedInWave;
    if (type == content::kNoDef) {
        return;  // an empty wave; the counter still advances so timing holds
    }
    const content::EnemyDef& def = ctx.content.enemy(type);

    Enemy enemy;
    enemy.type = type;
    enemy.position = Grid::centerOf(s.spawn);
    // Spread arrivals across the tile so a wave does not render as one blob.
    enemy.position.y += ctx.rng[RngStream::SpawnJitter].range(-0.25f, 0.25f);
    enemy.previousPosition = enemy.position;
    enemy.maxHealth = def.health * s.waveHealthScale;
    enemy.health = enemy.maxHealth;
    enemy.speed = def.speed * s.waveSpeedScale;

    // The pool stamps enemy.id from its own sequence.
    const EnemyId handle = s.enemies.create(enemy);
    const Enemy* created = s.enemies.get(handle);

    Event event = makeEvent(EventType::EnemySpawned, created != nullptr ? created->id : kInvalidId,
                            enemy.position);
    event.detail = type;
    event.value = enemy.maxHealth;
    ctx.events.push(event);
}

}  // namespace

// --- stats ------------------------------------------------------------------

const TowerStats& towerStats(Tower& tower, const content::ContentDb& content) {
    if (tower.statsDirty) {
        const content::TowerDef& def = content.tower(tower.type);
        TowerStats base;
        base.damage = def.damage;
        base.range = def.range;
        base.reloadTicks = def.reloadTicks;
        tower.stats = resolveStats(base, tower.mods);
        tower.statsDirty = false;
    }
    return tower.stats;
}

// --- commands ---------------------------------------------------------------

PlacementResult placeTower(SimContext& ctx, Cell cell, content::TowerDefId type) {
    WorldState& s = ctx.state;
    if (type >= ctx.content.towers().size()) {
        return PlacementResult::UnknownTowerType;
    }
    const content::TowerDef& def = ctx.content.tower(type);
    if (s.gold < def.cost) {
        return PlacementResult::NotEnoughGold;
    }
    if (!s.grid.inBounds(cell) || s.grid.at(cell) != Tile::Open || cell == s.spawn ||
        cell == s.goal) {
        return PlacementResult::BlockedTile;
    }
    if (!canPlaceTower(s.grid, cell, s.spawn, s.goal)) {
        return PlacementResult::WouldSealPath;
    }

    // Refuse to strand an enemy that is standing on the tile right now.
    for (const Enemy& enemy : s.enemies.items()) {
        if (Grid::cellOf(enemy.position) == cell) {
            return PlacementResult::BlockedTile;
        }
    }

    s.grid.set(cell, Tile::Tower);
    ++s.gridVersion;  // invalidates the placement preview memo
    s.flow.build(s.grid, s.goal);

    Tower tower;
    tower.cell = cell;
    tower.type = type;
    s.towers.create(tower);

    s.gold -= def.cost;
    return PlacementResult::Placed;
}

void commands(SimContext& ctx) {
    WorldState& s = ctx.state;
    for (const Command& command : s.commands.pending()) {
        switch (command.type) {
            case CommandType::PlaceTower: {
                const PlacementResult result = placeTower(ctx, command.cell, command.tower);
                const bool placed = result == PlacementResult::Placed;
                Event event =
                    makeEvent(placed ? EventType::TowerPlaced : EventType::TowerRejected,
                              placed ? s.towers.items()[s.towers.size() - 1].id : kInvalidId,
                              Grid::centerOf(command.cell));
                event.detail = static_cast<std::uint32_t>(result);
                event.value = static_cast<float>(command.tower);
                ctx.events.push(event);
                break;
            }
        }
    }
    s.commands.clear();
}

// --- waves ------------------------------------------------------------------

// How many enemies wave `wave` sends. Authored waves say so exactly; past the
// end of the authored list the endless rule takes over and grows from there.
int enemiesInWave(const content::ContentDb& content, int wave) {
    const content::WaveRules& rules = content.waves();
    const int authored = static_cast<int>(rules.waves.size());
    if (wave <= authored) {
        return rules.waves[static_cast<std::size_t>(wave - 1)].totalEnemies();
    }
    const int beyond = wave - authored;  // 1 for the first endless wave
    return rules.endless.firstCount + (beyond - 1) * rules.endless.addedPerWave;
}

void waveDirector(SimContext& ctx) {
    WorldState& s = ctx.state;
    if (s.enemiesLeftToSpawn > 0) {
        if (s.spawnTimer > 0) {
            --s.spawnTimer;
        }
        if (s.spawnTimer == 0) {
            spawnEnemy(ctx);
            --s.enemiesLeftToSpawn;
            s.spawnTimer = ctx.content.waves().spawnIntervalTicks;
        }
        return;
    }

    // Only start counting down to the next wave once the field is clear, so
    // the player always gets their full breather.
    if (!s.enemies.empty()) {
        return;
    }

    // A winnable level stops here rather than rolling into the next wave. The
    // check is before the gap timer so the run does not sit for six seconds
    // after the last enemy dies waiting to be told it is over.
    if (isWinnable(ctx.content) && s.wave >= static_cast<int>(ctx.content.waves().waves.size())) {
        return;
    }

    if (s.waveTimer > 0) {
        --s.waveTimer;
    }
    if (s.waveTimer == 0) {
        s.waveTimer = ctx.content.waves().gapTicks;
        startNextWave(ctx);
    }
}

// --- statuses ---------------------------------------------------------------

void statuses(SimContext& ctx) {
    WorldState& s = ctx.state;
    for (std::size_t i = 0; i < s.enemies.size(); ++i) {
        Enemy& enemy = s.enemies.items()[i];
        const EnemyId handle = s.enemies.handleAt(i);

        // Ascending def id, not slot order: the damage-over-time events are
        // queued in a canonical order for the same reason the queue is sorted
        // before it drains. See ADR 0011.
        for (content::StatusDefId id = 0; id < ctx.content.statuses().size(); ++id) {
            StatusSlot* live = nullptr;
            for (StatusSlot& slot : enemy.statuses) {
                if (slot.def == id) {
                    live = &slot;
                    break;
                }
            }
            if (live == nullptr) {
                continue;
            }

            const content::StatusDef& def = ctx.content.status(id);
            if (def.damagePerTick > 0.0f) {
                DamageEvent hit;
                hit.target = handle;
                hit.targetId = enemy.id;
                hit.amount = def.damagePerTick * static_cast<float>(live->stacks);
                hit.type = def.damageType;
                hit.kind = DamageSource::Status;
                // sourceId stays kInvalidId: an effect outlives the tower that
                // applied it, and attributing a burn to a sold tower would put
                // a dead entity's id in the canonical sort key.
                s.damage.push(hit);
            }

            if (live->remaining > 0) {
                --live->remaining;
            }
            if (live->remaining == 0) {
                *live = StatusSlot{};
            }
        }
    }
}

// --- enemies ----------------------------------------------------------------

void enemies(SimContext& ctx) {
    WorldState& s = ctx.state;
    for (Enemy& enemy : s.enemies.items()) {
        enemy.previousPosition = enemy.position;

        const Cell current = Grid::cellOf(enemy.position);

        // Steer toward the centre of the next tile on the flow field. Aiming
        // at tile centres keeps enemies off the corners of towers.
        const Cell nextCell = s.flow.next(current);
        const Vec2 target = Grid::centerOf(nextCell == current ? s.goal : nextCell);

        const Vec2 delta = target - enemy.position;
        const float distance = length(delta);
        const float speed = enemy.speed * statusSpeedMultiplier(enemy.statuses, ctx.content);
        const float travel = speed * config::kFixedDt;

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
    //
    // Collected first, destroyed second. Destroying inside the loop would
    // swap-and-pop an entity the loop has not visited yet into a slot it has
    // already passed, and that enemy would leak without costing a life.
    s.enemyRemovals.clear();
    for (std::size_t i = 0; i < s.enemies.size(); ++i) {
        const Enemy& enemy = s.enemies.items()[i];
        if (Grid::cellOf(enemy.position) != s.goal) {
            continue;
        }
        s.enemyRemovals.push_back(s.enemies.handleAt(i));
        --s.lives;
        ++s.leaked;

        Event event = makeEvent(EventType::EnemyLeaked, enemy.id, enemy.position);
        event.detail = enemy.type;
        ctx.events.push(event);
    }
    for (const EnemyId id : s.enemyRemovals) {
        s.enemies.destroy(id);
    }
}

void index(SimContext& ctx) {
    WorldState& s = ctx.state;
    s.enemyIndex.rebuild(s.grid.width(), s.grid.height(), s.enemies.items());
}

// --- towers -----------------------------------------------------------------

void towers(SimContext& ctx) {
    WorldState& s = ctx.state;
    for (std::size_t i = 0; i < s.towers.size(); ++i) {
        Tower& tower = s.towers.items()[i];
        // Every number this tower fights with is read here, on the tick it
        // fires. That is what makes a hot reload reach towers already standing
        // on the board rather than only the ones built after it.
        const content::TowerDef& def = ctx.content.tower(tower.type);
        const TowerStats& stats = towerStats(tower, ctx.content);

        if (tower.cooldown > 0) {
            --tower.cooldown;
        }

        const std::uint32_t slot =
            selectTarget<Enemy>(s.enemies.items(), s.enemyIndex, s.flow, tower.cell, stats.range);
        if (slot == kNoTarget) {
            continue;
        }
        const EnemyId targetId = s.enemies.handleAt(slot);
        const Enemy* target = s.enemies.get(targetId);
        if (target == nullptr) {
            continue;
        }

        const Vec2 origin = Grid::centerOf(tower.cell);
        tower.aim = normalized(target->position - origin);

        if (tower.cooldown > 0) {
            continue;
        }
        tower.cooldown = stats.reloadTicks;

        Projectile shot;
        shot.position = origin;
        shot.previousPosition = origin;
        shot.velocity = tower.aim * ctx.content.projectile(def.projectile).speed;
        shot.target = targetId;
        shot.targetId = target->id;
        shot.type = def.projectile;
        shot.damage = stats.damage;
        shot.damageType = def.damageType;
        shot.appliesStatus = def.appliesStatus;
        shot.source = s.towers.handleAt(i);
        shot.sourceId = tower.id;
        const ProjectileId created = s.projectiles.create(shot);

        Event event = makeEvent(EventType::TowerFired, tower.id, origin);
        event.source = target->id;
        event.detail = def.projectile;
        event.value = stats.damage;
        ctx.events.push(event);
        (void)created;
    }
}

// --- projectiles ------------------------------------------------------------

void projectiles(SimContext& ctx) {
    WorldState& s = ctx.state;
    s.projectileRemovals.clear();

    for (std::size_t i = 0; i < s.projectiles.size(); ++i) {
        Projectile& shot = s.projectiles.items()[i];
        const content::ProjectileDef& def = ctx.content.projectile(shot.type);
        ++shot.age;
        shot.previousPosition = shot.position;

        // One O(1) handle lookup, where this used to run two full linear scans
        // over every enemy for every projectile -- and the second scan asked the
        // same question as the first (known-wrong #3). A dead target resolves to
        // nullptr through the generation check rather than needing a search to
        // discover it is gone.
        // borrow() rather than get(): a Ref re-checks the pool's mutation epoch
        // on every dereference, so a future change that destroys an enemy
        // mid-iteration trips an assert in a debug build instead of silently
        // damaging the wrong enemy in a release one.
        auto target = s.enemies.borrow(shot.target);

        // Home in while the target lives; otherwise fly straight and expire.
        if (target) {
            shot.velocity = normalized(target->position - shot.position) * def.speed;
        }
        shot.position = shot.position + shot.velocity * config::kFixedDt;

        if (target) {
            const float hitRadius = def.hitRadius + ctx.content.enemy(target->type).radius;
            if (lengthSq(target->position - shot.position) <= hitRadius * hitRadius) {
                // Queued, never applied here. This is the change that makes the
                // damage formula auditable: there is one place health goes
                // down, and it is sys::damage.
                DamageEvent hit;
                hit.target = shot.target;
                hit.targetId = shot.targetId;
                hit.amount = shot.damage;
                hit.type = shot.damageType;
                hit.source = shot.source;
                hit.sourceId = shot.sourceId;
                hit.kind = DamageSource::Projectile;
                s.damage.push(hit);

                // The effect the firing tower applies, carried on the shot for
                // the same reason its damage is: the tower may be sold between
                // the shot leaving and the shot landing.
                if (shot.appliesStatus != content::kNoDef &&
                    shot.appliesStatus < ctx.content.statuses().size()) {
                    const content::StatusDef& status = ctx.content.status(shot.appliesStatus);
                    const StatusApply applied =
                        applyStatus(target->statuses, status, shot.appliesStatus);
                    if (applied != StatusApply::NoRoom && applied != StatusApply::Ignored) {
                        Event event =
                            makeEvent(EventType::StatusApplied, target->id, target->position);
                        event.source = shot.sourceId;
                        event.detail = shot.appliesStatus;
                        event.value =
                            static_cast<float>(statusStacks(target->statuses, shot.appliesStatus));
                        ctx.events.push(event);
                    }
                }

                shot.age = def.lifetimeTicks;  // consumed on impact
            }
        }

        if (shot.age >= def.lifetimeTicks) {
            s.projectileRemovals.push_back(s.projectiles.handleAt(i));
        }
    }

    for (const ProjectileId id : s.projectileRemovals) {
        s.projectiles.destroy(id);
    }
}

// --- damage -----------------------------------------------------------------

void damage(SimContext& ctx) {
    WorldState& s = ctx.state;
    s.damage.sortCanonically();
    s.enemyRemovals.clear();

    for (const DamageEvent& hit : s.damage.events()) {
        Enemy* enemy = s.enemies.get(hit.target);
        if (enemy == nullptr || enemy->id != hit.targetId) {
            continue;  // died earlier in this same drain
        }

        const float dealt = resolveDamage(hit, *enemy, ctx.content);
        enemy->health -= dealt;

        Event event = makeEvent(EventType::EnemyDamaged, enemy->id, enemy->position);
        event.source = hit.sourceId;
        event.detail = hit.type;
        event.value = dealt;
        ctx.events.push(event);

        if (enemy->health <= 0.0f) {
            s.enemyRemovals.push_back(hit.target);
        }
    }
    s.damage.clear();

    // Two hits can kill the same enemy in one tick, so the same handle can
    // appear twice. destroy() returns false the second time -- the generation
    // has already moved on -- which is what keeps the payout from doubling.
    // Relying on that rather than de-duplicating first is the point of a
    // generation handle: the stale case is handled, not avoided.
    for (const EnemyId id : s.enemyRemovals) {
        // The bounty has to be read before the enemy is destroyed, and get()
        // returns null for a handle a previous iteration already consumed.
        const Enemy* victim = s.enemies.get(id);
        if (victim == nullptr) {
            continue;
        }
        const int bounty = ctx.content.enemy(victim->type).bounty;
        const EntityId victimId = victim->id;
        const content::EnemyDefId victimType = victim->type;
        const Vec2 where = victim->position;
        if (s.enemies.destroy(id)) {
            s.gold += bounty;
            ++s.killed;

            Event event = makeEvent(EventType::EnemyKilled, victimId, where);
            event.detail = victimType;
            event.value = static_cast<float>(bounty);
            ctx.events.push(event);
        }
    }
}

// --- outcome ----------------------------------------------------------------

void outcome(SimContext& ctx) {
    WorldState& s = ctx.state;
    if (s.lives <= 0) {
        s.lives = 0;
        s.state = GameState::Defeat;
        ctx.events.push(makeEvent(EventType::Defeat, kInvalidId, Grid::centerOf(s.goal)));
        return;
    }

    // Won when the content declares an end and the board is clear of it: every
    // authored wave started, every one of them spawned out, and nothing left
    // walking.
    if (!isWinnable(ctx.content)) {
        return;
    }
    const int authored = static_cast<int>(ctx.content.waves().waves.size());
    if (s.wave >= authored && s.enemiesLeftToSpawn == 0 && s.enemies.empty()) {
        s.state = GameState::Victory;
        ctx.events.push(makeEvent(EventType::Victory, kInvalidId, Grid::centerOf(s.goal)));
    }
}

}  // namespace ktd::sys
