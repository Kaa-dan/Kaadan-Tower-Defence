#pragma once

#include "game/Grid.h"
#include "game/Span.h"
#include "game/Types.h"
#include "game/content/Parser.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The content database: every balance number the simulation reads, keyed by
// dense integer indices rather than strings.
//
// Two properties drive the design.
//
// The first is that **strings never appear in the simulation**. Loading is a
// two-pass affair -- pass one registers every id and assigns its index, pass
// two resolves the cross-references -- so by the time step() runs, a tower
// knows its type as a uint16 and nothing in a tick touches a std::string.
// That keeps the tick allocation-free and the state hash trivial.
//
// The second is that **entities store a def id, not baked stats**. A tower
// records that it is an "arrow" tower and reads its range from the database
// every tick. That is what lets a hot reload change the balance of towers that
// already exist, rather than only the ones built afterwards.
//
// Building a ContentDb by hand is a supported path, not a test-only hack:
// game/ has no filesystem and never will, so the database arrives as data
// whether it came from a parser or from code.

namespace ktd::content {

// Dense indices into the database's vectors. Distinct names for readability;
// they are deliberately plain integers so serialization stays trivial.
using TowerDefId = std::uint16_t;
using EnemyDefId = std::uint16_t;
using ProjectileDefId = std::uint16_t;

inline constexpr std::uint16_t kNoDef = 0xFFFFu;

struct ProjectileDef {
    std::string id;
    float speed = 0.0f;      // tiles per second
    float hitRadius = 0.0f;  // tiles
    std::uint32_t lifetimeTicks = 0;
};

struct TowerDef {
    std::string id;
    std::string name;
    int cost = 0;
    float range = 0.0f;  // tiles
    float damage = 0.0f;
    std::uint32_t reloadTicks = 0;
    ProjectileDefId projectile = kNoDef;
};

struct EnemyDef {
    std::string id;
    std::string name;
    float health = 0.0f;
    float speed = 0.0f;  // tiles per second
    float radius = 0.0f;
    int bounty = 0;  // gold paid on kill
};

// One authored wave: `counts[i]` enemies of type `enemies[i]`, spawned in
// order. Parallel arrays rather than a vector of pairs because that is how they
// are authored, and the loader checks the lengths match.
struct WaveDef {
    std::vector<EnemyDefId> enemies;
    std::vector<int> counts;

    int totalEnemies() const;
};

// What happens after the authored waves run out. A tower defence that stops at
// wave twelve is a demo, so the progression continues by rule -- and a level
// that authors no waves at all is pure endless, which is what the game does
// today.
struct EndlessRule {
    EnemyDefId enemy = kNoDef;
    int firstCount = 0;
    int addedPerWave = 0;
    float healthGrowth = 1.0f;  // compounding per wave
    float speedGrowth = 1.0f;
};

struct WaveRules {
    std::uint32_t spawnIntervalTicks = 0;  // between spawns within a wave
    std::uint32_t gapTicks = 0;            // between waves
    std::vector<WaveDef> waves;            // authored waves, in order
    EndlessRule endless;
};

struct LevelDef {
    std::string id;
    int width = 0;
    int height = 0;
    Cell spawn{};
    Cell goal{};
    std::vector<Tile> tiles;  // width * height, row-major, row 0 is y = 0
    int startingGold = 0;
    int startingLives = 0;

    Tile at(Cell cell) const;
};

// The loaded content, ready for the simulation.
//
// Everything is looked up by index in the hot path. The by-name lookups exist
// for load time and for tests, and are linear scans on purpose: the whole
// database is a few hundred entries and an unordered_map is banned in game/ for
// having an implementation-defined iteration order.
class ContentDb {
public:
    Span<const TowerDef> towers() const { return towers_; }
    Span<const EnemyDef> enemies() const { return enemies_; }
    Span<const ProjectileDef> projectiles() const { return projectiles_; }

    const TowerDef& tower(TowerDefId id) const { return towers_[id]; }
    const EnemyDef& enemy(EnemyDefId id) const { return enemies_[id]; }
    const ProjectileDef& projectile(ProjectileDefId id) const { return projectiles_[id]; }

    const LevelDef& level() const { return level_; }
    const WaveRules& waves() const { return waves_; }

    // kNoDef when the id is unknown. Load time only.
    TowerDefId findTower(std::string_view id) const;
    EnemyDefId findEnemy(std::string_view id) const;
    ProjectileDefId findProjectile(std::string_view id) const;

    // --- building ---------------------------------------------------------
    // References must already be resolved to ids, which is what forces the
    // loader's second pass to exist rather than being optional.
    TowerDefId addTower(TowerDef def);
    EnemyDefId addEnemy(EnemyDef def);
    ProjectileDefId addProjectile(ProjectileDef def);
    // Pass two's only job: the def is already in the vector, and this is how
    // its cross-reference gets filled in.
    void setTowerProjectile(TowerDefId tower, ProjectileDefId projectile) {
        towers_[tower].projectile = projectile;
    }
    void setLevel(LevelDef def) { level_ = std::move(def); }
    void setWaves(WaveRules rules) { waves_ = std::move(rules); }

    // A database the simulation can actually run: a level with a route, at
    // least one tower type and at least one enemy type. Checked once at load
    // rather than defended against on every tick.
    bool usable() const;

    // Canonical serialization, in the same shape and for the same reason as
    // World::serialize -- field order is the format, and adding a field to a
    // def means adding a line here.
    template <typename Sink>
    void serialize(Sink& out) const {
        out.u32(static_cast<std::uint32_t>(projectiles_.size()));
        for (const ProjectileDef& def : projectiles_) {
            writeString(out, def.id);
            out.f32(def.speed);
            out.f32(def.hitRadius);
            out.u32(def.lifetimeTicks);
        }

        out.u32(static_cast<std::uint32_t>(towers_.size()));
        for (const TowerDef& def : towers_) {
            writeString(out, def.id);
            out.i32(def.cost);
            out.f32(def.range);
            out.f32(def.damage);
            out.u32(def.reloadTicks);
            out.u32(def.projectile);
        }

        out.u32(static_cast<std::uint32_t>(enemies_.size()));
        for (const EnemyDef& def : enemies_) {
            writeString(out, def.id);
            out.f32(def.health);
            out.f32(def.speed);
            out.f32(def.radius);
            out.i32(def.bounty);
        }

        // Display names are absent on purpose: they are the one field that can
        // change without changing the run, and localisation should not
        // invalidate a replay.

        out.u32(waves_.spawnIntervalTicks);
        out.u32(waves_.gapTicks);
        out.u32(static_cast<std::uint32_t>(waves_.waves.size()));
        for (const WaveDef& wave : waves_.waves) {
            out.u32(static_cast<std::uint32_t>(wave.enemies.size()));
            for (std::size_t i = 0; i < wave.enemies.size(); ++i) {
                out.u32(wave.enemies[i]);
                out.i32(wave.counts[i]);
            }
        }
        out.u32(waves_.endless.enemy);
        out.i32(waves_.endless.firstCount);
        out.i32(waves_.endless.addedPerWave);
        out.f32(waves_.endless.healthGrowth);
        out.f32(waves_.endless.speedGrowth);

        writeString(out, level_.id);
        out.i32(level_.width);
        out.i32(level_.height);
        out.i32(level_.spawn.x);
        out.i32(level_.spawn.y);
        out.i32(level_.goal.x);
        out.i32(level_.goal.y);
        out.i32(level_.startingGold);
        out.i32(level_.startingLives);
        for (const Tile tile : level_.tiles) {
            out.u8(static_cast<std::uint8_t>(tile));
        }
    }

    // 64-bit fingerprint of the whole database.
    //
    // The world hashes this alongside its own state, so a replay recorded
    // against one set of balance numbers is detected rather than silently
    // producing a different run. That is exactly what hot reload does on
    // purpose -- "show me this run, rebalanced" is a different run -- and what
    // a stale save file does by accident.
    std::uint64_t fingerprint() const;

private:
    template <typename Sink>
    static void writeString(Sink& out, const std::string& text) {
        out.u32(static_cast<std::uint32_t>(text.size()));
        for (const char c : text) {
            out.u8(static_cast<std::uint8_t>(c));
        }
    }

    std::vector<TowerDef> towers_;
    std::vector<EnemyDef> enemies_;
    std::vector<ProjectileDef> projectiles_;
    LevelDef level_;
    WaveRules waves_;
};

// One named blob of text to load. The name is only ever used in error
// messages; game/ does not know it is a path and never opens it.
struct Source {
    std::string_view file;
    std::string_view text;
};

// Parses every source and builds the database from all of them together.
//
// Two passes over the parsed documents: the first registers every id, the
// second resolves cross-references. So a tower may name a projectile declared
// in a later file, and the load order of the content directory does not become
// load-bearing.
//
// Every problem found is appended to `errors` -- unknown references included --
// and `out` is left in whatever state the failure produced. The caller is
// expected to keep its previous database on failure rather than adopt this one.
bool load(Span<const Source> sources, ContentDb& out, std::vector<Error>& errors);

}  // namespace ktd::content
