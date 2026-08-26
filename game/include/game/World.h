#pragma once

#include "game/Commands.h"
#include "game/Config.h"
#include "game/Entity.h"
#include "game/Events.h"
#include "game/Span.h"
#include "game/Systems.h"
#include "game/Types.h"
#include "game/WorldState.h"
#include "game/content/Database.h"

#include <cstdint>

namespace ktd {

// The entire game simulation. Pure C++ with no platform or graphics
// dependency: it is driven by step() and observed through the accessors, so it
// runs identically inside the renderer-less test suite and on every platform.
//
// Phase 4 moved the rules out into free functions (Systems.h) and the fields
// into a plain struct (WorldState.h). What is left here is the lid: ownership,
// the fixed order the systems run in, and a read-only view for presentation.
class World {
public:
    // The database must outlive the world, and is not copied: a tower reads its
    // range through this pointer on the tick it fires.
    //
    // There is no default. A World with no content has no level to stand on and
    // no tower to build, and the constructor that used to supply a grid size
    // and inherit balance from a header is exactly what Phase 3 removed.
    explicit World(const content::ContentDb& content, std::uint64_t seed = 12345);

    // Advances the simulation by exactly one tick.
    //
    // There is deliberately no dt parameter. The simulation's clock is the tick
    // counter, and the fixed rate is a property of the world rather than
    // something the caller supplies -- a dt argument that nothing enforces is a
    // standing invitation to pass a frame delta during a debugging session and
    // quietly destroy reproducibility. config::kFixedDt still exists for the
    // spatial math inside a tick; it is not a knob.
    void step();

    // --- intent in ---------------------------------------------------------

    // Queues a command for the next step(). Returns false only when the queue
    // is full, which takes 64 actions inside one tick.
    //
    // This is the only way anything outside the simulation gets in, which is
    // what makes the replay log (Phase 7) exactly this stream plus tick
    // numbers.
    bool enqueue(const Command& command) { return state_.commands.push(command); }
    bool enqueuePlacement(Cell cell, content::TowerDefId type = 0) {
        return enqueue(Command{CommandType::PlaceTower, cell, type});
    }

    // Applies a placement immediately, outside the queue.
    //
    // Kept public for tests and for the scripted determinism scenario, which
    // want the answer on the spot rather than an event a tick later. The game
    // itself goes through the queue: `app/` must not call this, or the replay
    // log stops being complete.
    PlacementResult placeTower(Cell cell, content::TowerDefId type = 0);

    // Adds a stat modifier to a standing tower and marks its cache stale.
    // Returns false if the tower is gone or its mod slots are full.
    bool addTowerMod(TowerId tower, StatMod mod);

    // Stops and restarts the clock. A paused tick is a tick that never
    // happened, so this cannot change a run and is not a recorded command.
    void setPaused(bool paused);

    // --- facts out ---------------------------------------------------------

    // What happened during the most recent step(). Cleared at the top of the
    // next one, so presentation must drain it per step rather than per frame.
    Span<const Event> events() const { return state_.events.events(); }

    const content::ContentDb& content() const { return *content_; }

    // --- observation (rendering, UI, tests) ---
    const Grid& grid() const { return state_.grid; }

    // Bumped whenever a tile changes. Presentation uses it to know when a
    // baked copy of the board -- a static vertex buffer, say -- is stale,
    // which is cheaper than comparing the grid every frame.
    std::uint32_t gridVersion() const { return state_.gridVersion; }
    const FlowField& flowField() const { return state_.flow; }
    // Views, not containers. Callers only ever iterate, subscript and size
    // these, so naming the underlying storage in the signature would have made
    // Phase 2's pool migration a breaking change at every call site. See Span.h.
    Span<const Enemy> enemies() const { return state_.enemies.items(); }
    Span<const Tower> towers() const { return state_.towers.items(); }
    Span<const Projectile> projectiles() const { return state_.projectiles.items(); }

    // The handle for the tower at `index` in the view above, so a caller that
    // found a tower by looking can then address it. Handles are runtime
    // addresses and stay out of saves and tie-breaks (ADR 0009); naming one is
    // fine, and it is the only way to reach addTowerMod.
    TowerId towerAt(std::size_t index) const { return state_.towers.handleAt(index); }

    // The full answer the UI wants for a hover: whether this tower can go here
    // and, if not, why. Const, so the placement preview cannot accidentally
    // become a placement.
    //
    // The expensive half -- the BFS that proves the route survives -- is
    // memoized on the hover cell and a grid version counter, so a stationary
    // mouse costs two integer compares. The cheap half (gold, tile, type) is
    // rechecked every call, because gold changes on every kill and memoizing on
    // it would miss every time anyway.
    PlacementResult previewPlacement(Cell cell, content::TowerDefId type = 0) const;

    // Whether a tower may be placed here, ignoring cost and type.
    bool canPlaceAt(Cell cell) const;
    bool canAfford(content::TowerDefId type = 0) const;

    Cell spawn() const { return state_.spawn; }
    Cell goal() const { return state_.goal; }
    int gold() const { return state_.gold; }
    int lives() const { return state_.lives; }
    int waveNumber() const { return state_.wave; }
    std::uint64_t tick() const { return state_.tick; }
    // Ticks converted for display. The simulation itself never sees this.
    float timeUntilNextWave() const {
        return static_cast<float>(state_.waveTimer) * config::kFixedDt;
    }
    bool waveInProgress() const { return state_.enemiesLeftToSpawn > 0 || !state_.enemies.empty(); }
    GameState state() const { return state_.state; }
    int enemiesLeaked() const { return state_.leaked; }
    int enemiesKilled() const { return state_.killed; }

    // The canonical byte stream lives on WorldState, next to the fields it
    // writes. This forwards so that Hash.h and the Phase 7 save writer do not
    // have to know the state is a separate struct.
    template <typename Sink>
    void serialize(Sink& out) const {
        state_.serialize(out);
    }

    // 64-bit fingerprint of the entire simulation state. Two runs that agree on
    // this at every tick are the same run; a tolerance-based comparison can
    // only ever say they are similar.
    std::uint64_t checksum() const;

private:
    const content::ContentDb* content_;
    WorldState state_;

    // The placement preview memo. Not simulation state -- it is a cache of a
    // pure function of (cell, grid) -- so it lives here rather than in
    // WorldState, and is therefore deliberately outside the hash.
    mutable PlacementQuery placement_;
    mutable Cell placementCell_{-1, -1};
    mutable std::uint32_t placementVersion_ = 0;
    mutable bool placementResult_ = false;
    mutable bool placementCached_ = false;
};

}  // namespace ktd
