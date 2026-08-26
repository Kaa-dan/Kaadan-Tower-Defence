#include "game/World.h"

#include "game/Hash.h"

namespace ktd {

World::World(const content::ContentDb& content, std::uint64_t seed) : content_(&content) {
    state_.contentFingerprint = content.fingerprint();
    state_.rng = RngStreams(seed);

    // step() must not allocate, so every per-tick buffer gets its capacity here.
    // The numbers are headroom for a busy late wave, not hard limits: exceeding
    // one costs a single reallocation, not a bug.
    state_.enemies.reserve(512);
    state_.towers.reserve(128);
    state_.projectiles.reserve(512);
    state_.enemyRemovals.reserve(64);
    state_.projectileRemovals.reserve(128);
    state_.commands.reserve(kMaxPendingCommands);
    state_.events.reserve(256);
    state_.damage.reserve(256);

    // Level geometry, the route and the starting economy all arrive as data.
    const content::LevelDef& level = content.level();
    state_.grid = Grid(level.width, level.height);
    state_.spawn = level.spawn;
    state_.goal = level.goal;
    for (int y = 0; y < level.height; ++y) {
        for (int x = 0; x < level.width; ++x) {
            state_.grid.set({x, y}, level.at({x, y}));
        }
    }
    state_.gold = level.startingGold;
    state_.lives = level.startingLives;
    state_.waveTimer = content.waves().gapTicks;

    state_.flow.build(state_.grid, state_.goal);
}

std::uint64_t World::checksum() const { return hashWorld(*this); }

// The tick.
//
// This list is the whole scheduler. ADR 0004 chose a fixed compile-time order
// over a runtime one because a list of calls is deterministic by being obvious,
// and the order below is a design decision at every line: an enemy that moved
// after the towers looked for it would be shot at where it used to be, and
// damage applied before the projectiles moved would land a tick early.
void World::step() {
    if (state_.state != GameState::Playing) {
        return;
    }

    ++state_.tick;
    // Exactly one tick's worth of events, so presentation cannot be handed a
    // backlog whose size depends on the frame rate.
    state_.events.clear();

    SimContext ctx(state_, *content_);

    sys::commands(ctx);      // intent from outside, applied first
    sys::waveDirector(ctx);  // spawns arrive before anything can shoot them
    sys::statuses(ctx);      // burns queue their damage, durations age
    sys::enemies(ctx);       // movement, then leaks
    sys::index(ctx);         // after they move, before anyone looks
    sys::towers(ctx);        // target, aim, fire
    sys::projectiles(ctx);   // fly, hit, queue damage
    sys::damage(ctx);        // one formula, one place, canonical order
    sys::outcome(ctx);       // defeat and victory
}

PlacementResult World::placeTower(Cell cell, content::TowerDefId type) {
    SimContext ctx(state_, *content_);
    return sys::placeTower(ctx, cell, type);
}

bool World::addTowerMod(TowerId tower, StatMod mod) {
    Tower* target = state_.towers.get(tower);
    if (target == nullptr) {
        return false;
    }
    if (!target->mods.add(mod)) {
        return false;
    }
    target->statsDirty = true;
    return true;
}

void World::setPaused(bool paused) {
    if (paused && state_.state == GameState::Playing) {
        state_.state = GameState::Paused;
    } else if (!paused && state_.state == GameState::Paused) {
        state_.state = GameState::Playing;
    }
}

// --- placement preview -------------------------------------------------------

bool World::canAfford(content::TowerDefId type) const {
    return type < content_->towers().size() && state_.gold >= content_->tower(type).cost;
}

bool World::canPlaceAt(Cell cell) const {
    // The placement preview asks this every frame for a cell the mouse usually
    // has not left, and the answer can only change when a tower changes the
    // grid. Memoizing on (cell, gridVersion) turns the common case into two
    // integer compares; the miss runs one BFS over reused buffers, where the old
    // code copied the whole Grid and allocated a std::deque every single frame
    // (known-wrong #4).
    if (placementCached_ && placementVersion_ == state_.gridVersion && placementCell_ == cell) {
        return placementResult_;
    }

    bool result = false;
    if (state_.grid.inBounds(cell) && state_.grid.at(cell) == Tile::Open && cell != state_.spawn &&
        cell != state_.goal) {
        result = placement_.routeSurvives(state_.grid, cell, state_.spawn, state_.goal);
    }

    placementCached_ = true;
    placementCell_ = cell;
    placementVersion_ = state_.gridVersion;
    placementResult_ = result;
    return result;
}

PlacementResult World::previewPlacement(Cell cell, content::TowerDefId type) const {
    // Deliberately the same order of tests as sys::placeTower, so the preview
    // and the outcome cannot disagree about *why* something is refused. They
    // are two functions and not one because this one is const and cheap and
    // that one mutates and is not.
    if (type >= content_->towers().size()) {
        return PlacementResult::UnknownTowerType;
    }
    if (state_.gold < content_->tower(type).cost) {
        return PlacementResult::NotEnoughGold;
    }
    if (!state_.grid.inBounds(cell) || state_.grid.at(cell) != Tile::Open || cell == state_.spawn ||
        cell == state_.goal) {
        return PlacementResult::BlockedTile;
    }
    for (const Enemy& enemy : state_.enemies.items()) {
        if (Grid::cellOf(enemy.position) == cell) {
            return PlacementResult::BlockedTile;
        }
    }
    if (!canPlaceAt(cell)) {
        return PlacementResult::WouldSealPath;
    }
    return PlacementResult::Placed;
}

}  // namespace ktd
