#include "game/content/Database.h"

#include "game/Hasher.h"
#include "game/Path.h"

#include <utility>

namespace ktd::content {

namespace {

// Pulls typed fields out of one table, reporting a located error for anything
// missing, mistyped, or misspelled.
//
// Unknown keys are errors rather than being ignored. A silently dropped
// `dammage = 40` is the single most common way a data-driven game ships wrong
// numbers, and it is invisible precisely because everything still loads.
class Fields {
public:
    Fields(const Table& table, std::vector<Error>& errors) : table_(table), errors_(errors) {}

    void reject(const Value& value, std::string message) {
        errors_.push_back(Error{file_, value.line, value.column, std::move(message)});
    }

    void missing(std::string_view key) {
        errors_.push_back(
            Error{file_, table_.line, 1, "missing required key '" + std::string(key) + "'"});
    }

    void setFile(std::string file) { file_ = std::move(file); }

    bool has(std::string_view key) const { return table_.find(key) != nullptr; }

    const Value* get(std::string_view key, ValueKind wanted) {
        used_.push_back(key);
        const Value* value = table_.find(key);
        if (value == nullptr) {
            return nullptr;
        }
        const bool numericMatch = wanted == ValueKind::Float && value->isNumber();
        if (!numericMatch && value->kind != wanted) {
            reject(*value, std::string("'") + std::string(key) + "' should be a " + nameOf(wanted) +
                               ", not a " + nameOf(value->kind));
            return nullptr;
        }
        return value;
    }

    float requireFloat(std::string_view key) {
        const Value* value = get(key, ValueKind::Float);
        if (value == nullptr) {
            if (table_.find(key) == nullptr) {
                missing(key);
            }
            return 0.0f;
        }
        return value->asFloat();
    }

    float optionalFloat(std::string_view key, float fallback) {
        const Value* value = get(key, ValueKind::Float);
        return value == nullptr ? fallback : value->asFloat();
    }

    int requireInt(std::string_view key) {
        const Value* value = get(key, ValueKind::Integer);
        if (value == nullptr) {
            if (table_.find(key) == nullptr) {
                missing(key);
            }
            return 0;
        }
        return clampToInt(*value, key);
    }

    int optionalInt(std::string_view key, int fallback) {
        const Value* value = get(key, ValueKind::Integer);
        return value == nullptr ? fallback : clampToInt(*value, key);
    }

    std::uint32_t requireTicks(std::string_view key) {
        const Value* value = get(key, ValueKind::Integer);
        if (value == nullptr) {
            if (table_.find(key) == nullptr) {
                missing(key);
            }
            return 0;
        }
        if (value->integer < 0 || value->integer > 0xFFFFFFFF) {
            reject(*value, "'" + std::string(key) + "' must be a tick count from 0 upwards");
            return 0;
        }
        return static_cast<std::uint32_t>(value->integer);
    }

    std::string_view requireString(std::string_view key) {
        const Value* value = get(key, ValueKind::String);
        if (value == nullptr) {
            if (table_.find(key) == nullptr) {
                missing(key);
            }
            return {};
        }
        return value->text;
    }

    std::string_view optionalString(std::string_view key, std::string_view fallback) {
        const Value* value = get(key, ValueKind::String);
        return value == nullptr ? fallback : value->text;
    }

    // Returns null when absent; reports an error when present but not an array
    // of `element`.
    const Value* array(std::string_view key, ValueKind element) {
        const Value* value = get(key, ValueKind::Array);
        if (value == nullptr) {
            return nullptr;
        }
        if (value->elementKindKnown) {
            const bool numericMatch = element == ValueKind::Float && value->elements[0].isNumber();
            if (!numericMatch && value->elementKind != element) {
                reject(*value, std::string("'") + std::string(key) + "' should be an array of " +
                                   nameOf(element) + " values");
                return nullptr;
            }
        }
        return value;
    }

    // Called once every field has been read. Anything left over is a typo.
    void rejectUnknownKeys() {
        for (const KeyValue& entry : table_.entries) {
            bool known = false;
            for (const std::string_view key : used_) {
                if (entry.key == key) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                errors_.push_back(Error{file_, entry.line, entry.column,
                                        "unknown key '" + std::string(entry.key) + "'"});
            }
        }
    }

private:
    int clampToInt(const Value& value, std::string_view key) {
        if (value.integer < -2147483648ll || value.integer > 2147483647ll) {
            reject(value, "'" + std::string(key) + "' is out of range");
            return 0;
        }
        return static_cast<int>(value.integer);
    }

    std::string file_;
    const Table& table_;
    std::vector<Error>& errors_;
    std::vector<std::string_view> used_;
};

// A cross-reference that pass one recorded and pass two has to resolve.
struct PendingRef {
    std::string file;
    int line = 0;
    int column = 0;
    std::string target;  // the id that was named
};

PendingRef refAt(const std::string& file, const Table& table, std::string_view key,
                 std::string_view target) {
    const KeyValue* entry = nullptr;
    for (const KeyValue& candidate : table.entries) {
        if (candidate.key == key) {
            entry = &candidate;
        }
    }
    PendingRef ref;
    ref.file = file;
    ref.line = entry != nullptr ? entry->line : table.line;
    ref.column = entry != nullptr ? entry->column : 1;
    ref.target = std::string(target);
    return ref;
}

}  // namespace

// --- small accessors --------------------------------------------------------

int WaveDef::totalEnemies() const {
    int total = 0;
    for (const int count : counts) {
        total += count;
    }
    return total;
}

Tile LevelDef::at(Cell cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= height) {
        return Tile::Wall;
    }
    return tiles[static_cast<std::size_t>(cell.y * width + cell.x)];
}

TowerDefId ContentDb::findTower(std::string_view id) const {
    for (std::size_t i = 0; i < towers_.size(); ++i) {
        if (towers_[i].id == id) {
            return static_cast<TowerDefId>(i);
        }
    }
    return kNoDef;
}

EnemyDefId ContentDb::findEnemy(std::string_view id) const {
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        if (enemies_[i].id == id) {
            return static_cast<EnemyDefId>(i);
        }
    }
    return kNoDef;
}

ProjectileDefId ContentDb::findProjectile(std::string_view id) const {
    for (std::size_t i = 0; i < projectiles_.size(); ++i) {
        if (projectiles_[i].id == id) {
            return static_cast<ProjectileDefId>(i);
        }
    }
    return kNoDef;
}

TowerDefId ContentDb::addTower(TowerDef def) {
    towers_.push_back(std::move(def));
    return static_cast<TowerDefId>(towers_.size() - 1);
}

EnemyDefId ContentDb::addEnemy(EnemyDef def) {
    enemies_.push_back(std::move(def));
    return static_cast<EnemyDefId>(enemies_.size() - 1);
}

ProjectileDefId ContentDb::addProjectile(ProjectileDef def) {
    projectiles_.push_back(std::move(def));
    return static_cast<ProjectileDefId>(projectiles_.size() - 1);
}

bool ContentDb::usable() const {
    if (towers_.empty() || enemies_.empty()) {
        return false;
    }
    if (level_.width <= 0 || level_.height <= 0) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(level_.width * level_.height);
    return level_.tiles.size() == expected;
}

std::uint64_t ContentDb::fingerprint() const {
    Hasher hasher;
    serialize(hasher);
    return hasher.value();
}

// --- loading ----------------------------------------------------------------

bool load(Span<const Source> sources, ContentDb& out, std::vector<Error>& errors) {
    const std::size_t errorsBefore = errors.size();

    std::vector<Document> documents;
    documents.reserve(sources.size());
    for (const Source& source : sources) {
        Document document;
        parse(source.text, source.file, document, errors);
        documents.push_back(std::move(document));
    }

    // --- pass one: register every id, read every scalar --------------------
    //
    // Nothing here resolves a name to an index, because the thing being named
    // may not exist yet. That is the entire reason for two passes: content
    // files should not have to be ordered, and a load order that quietly
    // matters is a bug waiting for the day someone renames a file.

    struct PendingTower {
        TowerDefId id = kNoDef;
        PendingRef projectile;
    };
    struct PendingWave {
        std::size_t index = 0;
        std::vector<PendingRef> enemies;
        std::vector<int> counts;
    };

    std::vector<PendingTower> pendingTowers;
    std::vector<PendingWave> pendingWaves;
    PendingRef pendingEndlessEnemy;
    bool haveEndlessEnemy = false;
    bool haveLevel = false;
    bool haveWaveRules = false;
    WaveRules rules;

    for (const Document& document : documents) {
        for (const Table& table : document.tables) {
            Fields fields(table, errors);
            fields.setFile(document.file);

            const std::string_view kind = table.path.empty() ? std::string_view{} : table.path[0];
            const bool wantsArray = kind == "wave";
            if (table.arrayElement != wantsArray) {
                errors.push_back(Error{document.file, table.line, 1,
                                       wantsArray ? "waves are written [[wave]], not [wave]"
                                                  : "only [[wave]] may be an array of tables"});
                continue;
            }
            // Every kind but [waves] and [[wave]] is `[kind.id]`.
            if (kind != "waves" && kind != "wave" && table.path.size() != 2) {
                errors.push_back(Error{document.file, table.line, 1,
                                       "expected [" + std::string(kind) + ".<id>]"});
                continue;
            }

            if (kind == "projectile") {
                ProjectileDef def;
                def.id = std::string(table.leaf());
                if (out.findProjectile(def.id) != kNoDef) {
                    errors.push_back(Error{document.file, table.line, 1,
                                           "duplicate projectile '" + def.id + "'"});
                    continue;
                }
                def.speed = fields.requireFloat("speed");
                def.hitRadius = fields.requireFloat("hit_radius");
                def.lifetimeTicks = fields.requireTicks("lifetime_ticks");
                fields.rejectUnknownKeys();
                out.addProjectile(std::move(def));

            } else if (kind == "tower") {
                TowerDef def;
                def.id = std::string(table.leaf());
                if (out.findTower(def.id) != kNoDef) {
                    errors.push_back(
                        Error{document.file, table.line, 1, "duplicate tower '" + def.id + "'"});
                    continue;
                }
                def.name = std::string(fields.optionalString("name", table.leaf()));
                def.cost = fields.requireInt("cost");
                def.range = fields.requireFloat("range");
                def.damage = fields.requireFloat("damage");
                def.reloadTicks = fields.requireTicks("reload_ticks");
                const std::string_view projectile = fields.requireString("projectile");
                fields.rejectUnknownKeys();

                PendingTower pending;
                pending.projectile = refAt(document.file, table, "projectile", projectile);
                pending.id = out.addTower(std::move(def));
                pendingTowers.push_back(std::move(pending));

            } else if (kind == "enemy") {
                EnemyDef def;
                def.id = std::string(table.leaf());
                if (out.findEnemy(def.id) != kNoDef) {
                    errors.push_back(
                        Error{document.file, table.line, 1, "duplicate enemy '" + def.id + "'"});
                    continue;
                }
                def.name = std::string(fields.optionalString("name", table.leaf()));
                def.health = fields.requireFloat("health");
                def.speed = fields.requireFloat("speed");
                def.radius = fields.requireFloat("radius");
                def.bounty = fields.requireInt("bounty");
                fields.rejectUnknownKeys();
                out.addEnemy(std::move(def));

            } else if (kind == "level") {
                if (haveLevel) {
                    errors.push_back(Error{document.file, table.line, 1,
                                           "a second level was declared; one build holds one"});
                    continue;
                }
                haveLevel = true;

                LevelDef def;
                def.id = std::string(table.leaf());
                def.width = fields.requireInt("width");
                def.height = fields.requireInt("height");
                def.startingGold = fields.requireInt("starting_gold");
                def.startingLives = fields.requireInt("starting_lives");

                const auto readCell = [&](std::string_view key, Cell& cell) {
                    const Value* pair = fields.array(key, ValueKind::Integer);
                    if (pair == nullptr) {
                        // A present-but-wrong-typed value has already been
                        // reported by array(); only absence is news here.
                        if (!fields.has(key)) {
                            fields.missing(key);
                        }
                        return;
                    }
                    if (pair->elements.size() != 2) {
                        fields.reject(*pair, "'" + std::string(key) + "' must be [x, y]");
                        return;
                    }
                    cell.x = static_cast<int>(pair->elements[0].integer);
                    cell.y = static_cast<int>(pair->elements[1].integer);
                };
                readCell("spawn", def.spawn);
                readCell("goal", def.goal);

                if (def.width <= 0 || def.height <= 0 || def.width > 4096 || def.height > 4096) {
                    errors.push_back(Error{document.file, table.line, 1,
                                           "level width and height must be between 1 and 4096"});
                    fields.rejectUnknownKeys();
                    continue;
                }

                def.tiles.assign(static_cast<std::size_t>(def.width * def.height), Tile::Open);
                // The map is authored as one string per row, which is the only
                // layout a human can proofread against a screenshot.
                if (const Value* rows = fields.array("rows", ValueKind::String)) {
                    if (static_cast<int>(rows->elements.size()) != def.height) {
                        fields.reject(*rows, "'rows' has " + std::to_string(rows->elements.size()) +
                                                 " rows but height is " +
                                                 std::to_string(def.height));
                    } else {
                        for (int y = 0; y < def.height; ++y) {
                            const Value& row = rows->elements[static_cast<std::size_t>(y)];
                            if (static_cast<int>(row.text.size()) != def.width) {
                                fields.reject(row, "row " + std::to_string(y) + " is " +
                                                       std::to_string(row.text.size()) +
                                                       " cells wide but width is " +
                                                       std::to_string(def.width));
                                continue;
                            }
                            for (int x = 0; x < def.width; ++x) {
                                const char c = row.text[static_cast<std::size_t>(x)];
                                const std::size_t index =
                                    static_cast<std::size_t>(y * def.width + x);
                                if (c == '.') {
                                    def.tiles[index] = Tile::Open;
                                } else if (c == '#') {
                                    def.tiles[index] = Tile::Wall;
                                } else {
                                    errors.push_back(
                                        Error{document.file, row.line, row.column + 1 + x,
                                              std::string("'") + c +
                                                  "' is not a map character ('.' open, '#' wall)"});
                                }
                            }
                        }
                    }
                }

                const auto inBounds = [&](Cell cell) {
                    return cell.x >= 0 && cell.y >= 0 && cell.x < def.width && cell.y < def.height;
                };
                if (!inBounds(def.spawn) || !inBounds(def.goal)) {
                    errors.push_back(Error{document.file, table.line, 1,
                                           "spawn and goal must be inside the level"});
                } else if (def.spawn == def.goal) {
                    errors.push_back(
                        Error{document.file, table.line, 1, "spawn and goal are the same cell"});
                } else if (def.at(def.spawn) != Tile::Open || def.at(def.goal) != Tile::Open) {
                    // Walling the spawn or goal in is not a route the player
                    // can ever open, so it is a content bug, not a hard level.
                    errors.push_back(Error{document.file, table.line, 1,
                                           "spawn and goal must sit on open tiles"});
                } else {
                    // And the route has to exist before a tower is ever placed.
                    // A level whose lane is walled off by a typo loads fine and
                    // then presents enemies standing still at the entrance,
                    // which is a mystifying way to discover a missing '.'.
                    Grid grid(def.width, def.height);
                    for (int y = 0; y < def.height; ++y) {
                        for (int x = 0; x < def.width; ++x) {
                            grid.set({x, y}, def.at({x, y}));
                        }
                    }
                    FlowField flow;
                    flow.build(grid, def.goal);
                    if (!flow.reachable(def.spawn)) {
                        errors.push_back(
                            Error{document.file, table.line, 1, "no route from spawn to goal"});
                    }
                }

                fields.rejectUnknownKeys();
                out.setLevel(std::move(def));

            } else if (kind == "waves") {
                if (haveWaveRules) {
                    errors.push_back(
                        Error{document.file, table.line, 1, "a second [waves] table was declared"});
                    continue;
                }
                haveWaveRules = true;
                rules.spawnIntervalTicks = fields.requireTicks("spawn_interval_ticks");
                rules.gapTicks = fields.requireTicks("gap_ticks");
                rules.endless.firstCount = fields.requireInt("endless_first_count");
                rules.endless.addedPerWave = fields.requireInt("endless_added_per_wave");
                rules.endless.healthGrowth = fields.requireFloat("health_growth");
                rules.endless.speedGrowth = fields.requireFloat("speed_growth");
                const std::string_view enemy = fields.requireString("endless_enemy");
                fields.rejectUnknownKeys();
                pendingEndlessEnemy = refAt(document.file, table, "endless_enemy", enemy);
                haveEndlessEnemy = true;

            } else if (kind == "wave") {
                PendingWave pending;
                pending.index = rules.waves.size();
                const Value* enemies = fields.array("enemies", ValueKind::String);
                const Value* counts = fields.array("counts", ValueKind::Integer);
                fields.rejectUnknownKeys();
                if (enemies == nullptr || counts == nullptr) {
                    errors.push_back(Error{document.file, table.line, 1,
                                           "a wave needs 'enemies' and 'counts' arrays"});
                    continue;
                }
                if (enemies->elements.size() != counts->elements.size()) {
                    fields.reject(*counts, "'counts' has " +
                                               std::to_string(counts->elements.size()) +
                                               " entries but 'enemies' has " +
                                               std::to_string(enemies->elements.size()));
                    continue;
                }
                for (std::size_t i = 0; i < enemies->elements.size(); ++i) {
                    const Value& name = enemies->elements[i];
                    PendingRef ref;
                    ref.file = document.file;
                    ref.line = name.line;
                    ref.column = name.column;
                    ref.target = std::string(name.text);
                    pending.enemies.push_back(std::move(ref));
                    pending.counts.push_back(static_cast<int>(counts->elements[i].integer));
                }
                rules.waves.push_back(WaveDef{});
                pendingWaves.push_back(std::move(pending));

            } else {
                errors.push_back(Error{document.file, table.line, 1,
                                       "unknown table '" + std::string(kind) + "'"});
            }
        }
    }

    // --- pass two: resolve every cross-reference ---------------------------

    const auto resolveEnemy = [&](const PendingRef& ref) {
        const EnemyDefId id = out.findEnemy(ref.target);
        if (id == kNoDef) {
            errors.push_back(
                Error{ref.file, ref.line, ref.column, "no enemy named '" + ref.target + "'"});
        }
        return id;
    };

    for (const PendingTower& pending : pendingTowers) {
        const ProjectileDefId id = out.findProjectile(pending.projectile.target);
        if (id == kNoDef) {
            errors.push_back(Error{pending.projectile.file, pending.projectile.line,
                                   pending.projectile.column,
                                   "no projectile named '" + pending.projectile.target + "'"});
            continue;
        }
        out.setTowerProjectile(pending.id, id);
    }

    for (const PendingWave& pending : pendingWaves) {
        WaveDef wave;
        for (std::size_t i = 0; i < pending.enemies.size(); ++i) {
            const EnemyDefId id = resolveEnemy(pending.enemies[i]);
            if (id == kNoDef) {
                continue;
            }
            wave.enemies.push_back(id);
            wave.counts.push_back(pending.counts[i]);
        }
        rules.waves[pending.index] = std::move(wave);
    }

    if (haveEndlessEnemy) {
        rules.endless.enemy = resolveEnemy(pendingEndlessEnemy);
    }
    out.setWaves(std::move(rules));

    if (!haveLevel) {
        errors.push_back(Error{std::string(sources.empty() ? "content" : sources[0].file), 0, 0,
                               "no [level.<id>] table was declared"});
    }
    if (!haveWaveRules) {
        errors.push_back(Error{std::string(sources.empty() ? "content" : sources[0].file), 0, 0,
                               "no [waves] table was declared"});
    }

    return errors.size() == errorsBefore;
}

}  // namespace ktd::content
