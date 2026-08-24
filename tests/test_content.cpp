#include "TestFramework.h"

#include "game/World.h"
#include "game/content/Database.h"
#include "game/content/Parser.h"

#include <bit>
#include <string>

using namespace ktd;
using namespace ktd::content;

namespace {

// Parse one blob and return the errors, so the tests below can assert on
// message text and location rather than only on success or failure.
std::vector<Error> errorsFrom(std::string_view text) {
    Document document;
    std::vector<Error> errors;
    parse(text, "test.ktd", document, errors);
    return errors;
}

// Exact float comparison, spelled as bits. -Wfloat-equal is on for good reason
// in the simulation, but these tests exist precisely to pin exact values, and a
// tolerance would not see the one-ulp difference the whole ADR is about.
std::uint32_t bitsOf(float value) { return std::bit_cast<std::uint32_t>(value); }

bool mentions(const Error& error, std::string_view fragment) {
    return error.message.find(fragment) != std::string::npos;
}

}  // namespace

// --- the parser -------------------------------------------------------------

TEST(parse_reads_the_documented_subset) {
    const std::string_view text = R"(
# a comment, and a blank line above
[tower.arrow]
name = "Arrow Tower"   # trailing comments are fine
cost = 40
range = 3.2
enabled = true
tags = ["basic", "physical"]

[[wave]]
counts = [
    4,   # arrays may span lines
    2,
]
)";
    Document document;
    std::vector<Error> errors;
    CHECK(parse(text, "test.ktd", document, errors));
    CHECK(errors.empty());
    CHECK_EQ(document.tables.size(), std::size_t{2});

    const Table& tower = document.tables[0];
    CHECK_EQ(tower.path.size(), std::size_t{2});
    CHECK(tower.path[0] == "tower");
    CHECK(tower.leaf() == "arrow");
    CHECK(!tower.arrayElement);
    CHECK(tower.find("name")->text == "Arrow Tower");
    CHECK_EQ(tower.find("cost")->integer, std::int64_t{40});
    CHECK_EQ(bitsOf(tower.find("range")->number), bitsOf(3.2f));
    CHECK_EQ(tower.find("enabled")->integer, std::int64_t{1});
    CHECK_EQ(tower.find("tags")->elements.size(), std::size_t{2});
    CHECK(tower.find("tags")->elements[1].text == "physical");

    const Table& wave = document.tables[1];
    CHECK(wave.arrayElement);
    CHECK_EQ(wave.find("counts")->elements.size(), std::size_t{2});
    CHECK_EQ(wave.find("counts")->elements[0].integer, std::int64_t{4});
}

// The criterion: errors name file, line and column, and the parser keeps going
// so a session of editing a balance file does not cost one rebuild per typo.
TEST(parse_rejects_malformed_input_with_line_numbers) {
    const std::string_view text =
        "[tower.arrow]\n"         // 1
        "cost 40\n"               // 2  missing '='
        "range = @\n"             // 3  not a value
        "name = \"unclosed\n"     // 4  unterminated string
        "damage = 10 20\n"        // 5  trailing junk
        "damage = 5\n"            // 6  duplicate key
        "[tower.arrow]\n"         // 7  duplicate table
        "tags = [1, \"two\"]\n";  // 8  mixed array

    const std::vector<Error> errors = errorsFrom(text);

    // Every one of the seven problems is reported, not just the first.
    CHECK_EQ(errors.size(), std::size_t{7});

    CHECK_EQ(errors[0].line, 2);
    CHECK(mentions(errors[0], "expected '='"));
    CHECK_EQ(errors[1].line, 3);
    CHECK(mentions(errors[1], "expected a value"));
    CHECK_EQ(errors[2].line, 4);
    CHECK(mentions(errors[2], "unterminated string"));
    CHECK_EQ(errors[3].line, 5);
    CHECK(mentions(errors[3], "unexpected text after a value"));
    CHECK_EQ(errors[4].line, 6);
    CHECK(mentions(errors[4], "duplicate key"));
    CHECK_EQ(errors[5].line, 7);
    CHECK(mentions(errors[5], "duplicate table"));
    CHECK_EQ(errors[6].line, 8);
    CHECK(mentions(errors[6], "mixes"));

    // Columns point at the offending token, not at the start of the line.
    CHECK_EQ(errors[1].column, 9);   // the '@' in "range = @"
    CHECK_EQ(errors[3].column, 13);  // the stray "20"

    // And the message is printable in the form an editor can jump to.
    CHECK(errors[0].format() == "test.ktd:2:6: expected '=' after key 'cost'");
}

TEST(parse_keeps_going_after_a_bad_line) {
    // The line after a broken one is still read. Without resynchronisation a
    // single stray character would hide every remaining problem in the file.
    Document document;
    std::vector<Error> errors;
    parse("[tower.arrow]\nrange = @\ncost = 40\n", "test.ktd", document, errors);
    CHECK_EQ(errors.size(), std::size_t{1});
    CHECK_EQ(document.tables.size(), std::size_t{1});
    CHECK(document.tables[0].find("cost") != nullptr);
    CHECK(document.tables[0].find("range") == nullptr);
}

// This is the criterion that justifies hand-rolling the parser at all (ADR
// 0005). strtof and from_chars are correctly rounded per spec, but the spec is
// not what ships -- three standard libraries do, and their quality varies. So
// the conversion is pinned to exact bit patterns rather than to a tolerance:
// a value that is one ulp out on Windows is a cross-platform desync, and
// CHECK_NEAR would not see it.
TEST(parse_float_is_deterministic) {
    struct Case {
        const char* text;
        std::uint32_t bits;
    };
    // Every expected value is the IEEE-754 binary32 encoding of the correctly
    // rounded result, which is also what the C++ literal of the same digits
    // produces -- asserted below, so the table cannot drift from the language.
    static constexpr Case kCases[] = {
        {"0", 0x00000000u},      {"1", 0x3F800000u},        {"-1", 0xBF800000u},
        {"0.5", 0x3F000000u},    {"3.2", 0x404CCCCDu},      {"1.18", 0x3F970A3Du},
        {"1.02", 0x3F828F5Cu},   {"0.18", 0x3E3851ECu},     {"0.22", 0x3E6147AEu},
        {"1.6", 0x3FCCCCCDu},    {"9.0", 0x41100000u},      {"24.0", 0x41C00000u},
        {"0.75", 0x3F400000u},   {"-0.25", 0xBE800000u},    {"1e3", 0x447A0000u},
        {"1.5e-3", 0x3AC49BA6u}, {"0.000001", 0x358637BDu}, {"123456789", 0x4CEB79A3u},
    };
    static constexpr float kLiterals[] = {
        0.0f, 1.0f, -1.0f, 0.5f,  3.2f,   1.18f, 1.02f,   0.18f,     0.22f,
        1.6f, 9.0f, 24.0f, 0.75f, -0.25f, 1e3f,  1.5e-3f, 0.000001f, 123456789.0f,
    };

    for (std::size_t i = 0; i < std::size(kCases); ++i) {
        float parsed = 0.0f;
        CHECK(parseFloatExact(kCases[i].text, parsed));
        CHECK_EQ(std::bit_cast<std::uint32_t>(parsed), kCases[i].bits);
        CHECK_EQ(std::bit_cast<std::uint32_t>(kLiterals[i]), kCases[i].bits);
    }
}

// The exact path has a documented edge, and past it the parser refuses rather
// than rounding. A silently-rounded balance number is the exact failure this
// whole approach exists to prevent.
TEST(parse_float_refuses_what_it_cannot_convert_exactly) {
    float value = 0.0f;
    CHECK(!parseFloatExact("1.2345678901234567", value));  // 17 significant digits
    CHECK(!parseFloatExact("1e30", value));                // exponent past 10^22
    CHECK(!parseFloatExact("1e-30", value));
    CHECK(!parseFloatExact("", value));
    CHECK(!parseFloatExact("1.2.3", value));
    CHECK(!parseFloatExact("1.0f", value));
    CHECK(!parseFloatExact("nan", value));
    CHECK(!parseFloatExact("0x10", value));
    CHECK(!parseFloatExact("1e", value));

    // Leading zeros cost nothing against the significant-digit budget.
    CHECK(parseFloatExact("0.000000000000000000001", value));
    CHECK_EQ(bitsOf(value), bitsOf(1e-21f));
}

// --- the database -----------------------------------------------------------

namespace {

// A complete, valid content set, used as the base for the failure cases so
// each of those differs in exactly the thing under test.
constexpr std::string_view kGoodContent = R"(
[projectile.bolt]
speed = 9.0
hit_radius = 0.18
lifetime_ticks = 180

[tower.arrow]
name = "Arrow Tower"
cost = 40
range = 3.2
damage = 10.0
reload_ticks = 38
projectile = "bolt"

[enemy.grunt]
health = 24.0
speed = 1.6
radius = 0.22
bounty = 8

[level.field]
width = 6
height = 3
spawn = [0, 1]
goal = [5, 1]
starting_gold = 120
starting_lives = 20
rows = [
    "......",
    "......",
    "......",
]

[waves]
spawn_interval_ticks = 45
gap_ticks = 360
endless_enemy = "grunt"
endless_first_count = 6
endless_added_per_wave = 2
health_growth = 1.18
speed_growth = 1.02
)";

bool loadText(std::string_view text, ContentDb& db, std::vector<Error>& errors) {
    const Source source{"test.ktd", text};
    return load(Span<const Source>(&source, 1), db, errors);
}

}  // namespace

TEST(content_loads_a_complete_definition_set) {
    ContentDb db;
    std::vector<Error> errors;
    CHECK(loadText(kGoodContent, db, errors));
    for (const Error& error : errors) {
        std::printf("    unexpected: %s\n", error.format().c_str());
    }
    CHECK(db.usable());

    CHECK_EQ(db.towers().size(), std::size_t{1});
    const TowerDef& tower = db.tower(0);
    CHECK(tower.name == "Arrow Tower");
    CHECK_EQ(tower.cost, 40);
    CHECK_EQ(bitsOf(tower.range), bitsOf(3.2f));
    CHECK_EQ(tower.reloadTicks, std::uint32_t{38});
    // The reference resolved to a dense index, and the string is gone.
    CHECK_EQ(tower.projectile, ProjectileDefId{0});
    CHECK_EQ(bitsOf(db.projectile(tower.projectile).speed), bitsOf(9.0f));

    CHECK_EQ(bitsOf(db.enemy(0).health), bitsOf(24.0f));
    CHECK_EQ(db.enemy(0).bounty, 8);
    // An absent display name falls back to the id rather than being empty.
    CHECK(db.enemy(0).name == "grunt");

    CHECK_EQ(db.level().width, 6);
    CHECK(db.level().spawn == Cell{0, 1});
    CHECK(db.level().goal == Cell{5, 1});
    CHECK_EQ(db.level().startingGold, 120);
    CHECK_EQ(db.waves().spawnIntervalTicks, std::uint32_t{45});
    CHECK_EQ(db.waves().endless.enemy, EnemyDefId{0});
}

// Declaration order must not matter: a tower may name a projectile defined
// later, or in another file entirely. That is what the second pass buys, and
// without a test the two passes would quietly collapse into one.
TEST(content_resolves_references_across_files_in_any_order) {
    constexpr std::string_view kTowers = R"(
[tower.arrow]
cost = 40
range = 3.2
damage = 10.0
reload_ticks = 38
projectile = "bolt"
)";
    constexpr std::string_view kRest = R"(
[projectile.bolt]
speed = 9.0
hit_radius = 0.18
lifetime_ticks = 180

[enemy.grunt]
health = 24.0
speed = 1.6
radius = 0.22
bounty = 8

[level.field]
width = 4
height = 3
spawn = [0, 1]
goal = [3, 1]
starting_gold = 120
starting_lives = 20

[waves]
spawn_interval_ticks = 45
gap_ticks = 360
endless_enemy = "grunt"
endless_first_count = 6
endless_added_per_wave = 2
health_growth = 1.18
speed_growth = 1.02
)";
    // Towers first, so every reference in it points forward.
    const Source sources[] = {{"towers.ktd", kTowers}, {"rest.ktd", kRest}};
    ContentDb db;
    std::vector<Error> errors;
    CHECK(load(Span<const Source>(sources, 2), db, errors));
    CHECK_EQ(db.tower(0).projectile, ProjectileDefId{0});
    // A level with no rows is all open, not an error.
    CHECK_EQ(db.level().tiles.size(), std::size_t{12});
    CHECK(db.level().at({2, 0}) == Tile::Open);
}

// The criterion: a typo in a reference fails at load, loudly, rather than
// becoming a null projectile discovered at wave forty.
TEST(content_unresolved_reference_is_an_error) {
    std::string text(kGoodContent);
    const std::size_t at = text.find("projectile = \"bolt\"");
    text.replace(at, std::string_view("projectile = \"bolt\"").size(),
                 "projectile = \"typo\"     ");

    ContentDb db;
    std::vector<Error> errors;
    CHECK(!loadText(text, db, errors));
    CHECK_EQ(errors.size(), std::size_t{1});
    CHECK(mentions(errors[0], "no projectile named 'typo'"));
    // Located at the reference itself, which is the only place worth looking.
    CHECK_EQ(errors[0].line, 13);
}

TEST(content_rejects_a_misspelled_key) {
    // A key nobody reads is worse than a missing one: everything loads and the
    // number is silently the default.
    std::string text(kGoodContent);
    const std::size_t at = text.find("damage = 10.0");
    text.replace(at, std::string_view("damage").size(), "dammag");

    ContentDb db;
    std::vector<Error> errors;
    CHECK(!loadText(text, db, errors));
    CHECK_EQ(errors.size(), std::size_t{2});
    CHECK(mentions(errors[0], "missing required key 'damage'"));
    CHECK(mentions(errors[1], "unknown key 'dammag'"));
}

TEST(content_rejects_an_unroutable_level) {
    std::string text(kGoodContent);
    const std::size_t at = text.find("spawn = [0, 1]");
    text.replace(at, std::string_view("spawn = [0, 1]").size(), "spawn = [9, 9]");

    ContentDb db;
    std::vector<Error> errors;
    CHECK(!loadText(text, db, errors));
    CHECK(mentions(errors[0], "inside the level"));
}

TEST(content_reads_a_level_map_and_locates_a_bad_cell) {
    std::string text(kGoodContent);
    const std::size_t at = text.find("\"......\",\n    \"......\",\n    \"......\",");
    text.replace(at, std::string_view("\"......\",\n    \"......\",\n    \"......\",").size(),
                 "\"..##..\",\n    \"......\",\n    \"..?...\",");

    ContentDb db;
    std::vector<Error> errors;
    CHECK(!loadText(text, db, errors));
    CHECK(mentions(errors[0], "not a map character"));
    // Column points at the offending character inside the row string.
    CHECK_EQ(errors[0].column, 8);
}

TEST(content_fingerprint_tracks_every_number) {
    ContentDb base;
    std::vector<Error> errors;
    CHECK(loadText(kGoodContent, base, errors));

    ContentDb same;
    CHECK(loadText(kGoodContent, same, errors));
    CHECK_EQ(base.fingerprint(), same.fingerprint());

    std::string rebalanced(kGoodContent);
    rebalanced.replace(rebalanced.find("damage = 10.0"), std::string_view("damage = 10.0").size(),
                       "damage = 11.0");
    ContentDb changed;
    CHECK(loadText(rebalanced, changed, errors));
    CHECK(base.fingerprint() != changed.fingerprint());

    // A display name is not part of the run, so localising the game must not
    // invalidate a replay.
    std::string renamed(kGoodContent);
    renamed.replace(renamed.find("\"Arrow Tower\""), std::string_view("\"Arrow Tower\"").size(),
                    "\"Pfeilturm\"");
    ContentDb localised;
    CHECK(loadText(renamed, localised, errors));
    CHECK_EQ(base.fingerprint(), localised.fingerprint());
}

// --- content driving the simulation -----------------------------------------

namespace {

// A whole game's worth of content assembled in code, with no parser and no
// file anywhere in sight.
//
// This is the shape the console ports need -- and the editor, and any test that
// wants a board it fully controls. It is a supported path rather than a
// workaround, which is why ContentDb has a building API at all.
ContentDb builtInCode() {
    ContentDb db;

    ProjectileDef bolt;
    bolt.id = "bolt";
    bolt.speed = 9.0f;
    bolt.hitRadius = 0.18f;
    bolt.lifetimeTicks = 180;
    const ProjectileDefId boltId = db.addProjectile(std::move(bolt));

    // Two towers identical in every respect except how hard they hit, so a
    // difference in outcome can only come from the damage number.
    TowerDef pea;
    pea.id = "pea";
    pea.name = "Pea Shooter";
    pea.cost = 20;
    pea.range = 3.0f;
    pea.damage = 2.0f;
    pea.reloadTicks = 30;
    pea.projectile = boltId;
    db.addTower(std::move(pea));

    TowerDef heavy;
    heavy.id = "heavy";
    heavy.name = "Heavy";
    heavy.cost = 20;
    heavy.range = 3.0f;
    heavy.damage = 60.0f;
    heavy.reloadTicks = 30;
    heavy.projectile = boltId;
    db.addTower(std::move(heavy));

    EnemyDef grunt;
    grunt.id = "grunt";
    grunt.health = 30.0f;
    grunt.speed = 1.5f;
    grunt.radius = 0.22f;
    grunt.bounty = 5;
    db.addEnemy(std::move(grunt));

    LevelDef level;
    level.id = "lane";
    level.width = 14;
    level.height = 5;
    level.spawn = {0, 2};
    level.goal = {13, 2};
    level.tiles.assign(static_cast<std::size_t>(level.width * level.height), Tile::Open);
    level.startingGold = 100;
    level.startingLives = 5;
    db.setLevel(std::move(level));

    WaveRules rules;
    rules.spawnIntervalTicks = 30;
    rules.gapTicks = 60;
    rules.endless.enemy = 0;
    rules.endless.firstCount = 6;
    rules.endless.addedPerWave = 1;
    rules.endless.healthGrowth = 1.0f;
    rules.endless.speedGrowth = 1.0f;
    db.setWaves(std::move(rules));
    return db;
}

void advance(World& world, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.step();
    }
}

}  // namespace

// The criterion that keeps the layering honest: game/ never opens a file, so a
// database handed to it from memory has to be enough to run the whole
// simulation. If this ever needs a path, purity has been lost.
TEST(world_runs_from_a_database_built_in_code) {
    const ContentDb db = builtInCode();
    CHECK(db.usable());

    World world(db);
    CHECK_EQ(world.gold(), 100);
    CHECK_EQ(world.lives(), 5);
    CHECK_EQ(world.grid().width(), 14);
    CHECK_EQ(world.grid().height(), 5);
    CHECK(world.spawn() == Cell{0, 2});

    // The wave director runs on the authored gap and interval, not on anything
    // left behind in a header.
    advance(world, 60);
    CHECK_EQ(world.waveNumber(), 1);
    advance(world, 1);
    CHECK_EQ(static_cast<int>(world.enemies().size()), 1);
    CHECK_EQ(bitsOf(world.enemies()[0].health), bitsOf(30.0f));
    CHECK_EQ(world.enemies()[0].type, EnemyDefId{0});

    // Undefended, they walk the lane and cost lives.
    advance(world, 900);
    CHECK(world.enemiesLeaked() > 0);
    CHECK(world.lives() < 5);
}

// The thing that was impossible before this phase: two tower types, differing
// only in data, producing different outcomes in the same world.
//
// The two are identical in cost, range, reload and projectile, so gold spent
// and shots fired match tick for tick and the only variable left is damage.
TEST(two_tower_types_behave_differently) {
    const ContentDb db = builtInCode();
    CHECK_EQ(db.towers().size(), std::size_t{2});

    constexpr TowerDefId kPea = 0;
    constexpr TowerDefId kHeavy = 1;
    constexpr int kTicks = 1200;

    const auto runWith = [&](TowerDefId type) {
        World world(db, 4242);
        // Two towers flanking the lane, on tiles the route does not use.
        CHECK(world.placeTower({4, 1}, type) == PlacementResult::Placed);
        CHECK(world.placeTower({4, 3}, type) == PlacementResult::Placed);
        advance(world, kTicks);
        return world;
    };

    const World peas = runWith(kPea);
    const World heavies = runWith(kHeavy);

    // Same cost, so the economy is not what separates them.
    CHECK_EQ(db.tower(kPea).cost, db.tower(kHeavy).cost);

    // 60 damage kills a 30-health grunt with one hit; 2 damage needs fifteen,
    // so most of what the pea shooters engage walks out of range still alive.
    CHECK(heavies.enemiesKilled() > peas.enemiesKilled());
    CHECK(heavies.enemiesLeaked() < peas.enemiesLeaked());
    CHECK(heavies.lives() > peas.lives());

    // And the towers remember which type they are, which is what a hot reload
    // needs in order to reach a tower that is already standing.
    CHECK_EQ(heavies.towers()[0].type, kHeavy);
    CHECK_EQ(peas.towers()[0].type, kPea);
}

TEST(placing_an_unknown_tower_type_is_refused) {
    const ContentDb db = builtInCode();
    World world(db);
    CHECK(world.placeTower({4, 1}, 7) == PlacementResult::UnknownTowerType);
    CHECK(world.towers().empty());
    CHECK_EQ(world.gold(), 100);
    CHECK(!world.canAfford(7));
}

// Authored waves arrive exactly as written and are not scaled; the endless rule
// takes over after them and compounds from its own first wave, so writing an
// opening wave does not shift the curve behind it.
TEST(waves_run_authored_first_then_endless) {
    ContentDb db = builtInCode();

    EnemyDef runner;
    runner.id = "runner";
    runner.health = 8.0f;
    runner.speed = 3.0f;
    runner.radius = 0.18f;
    runner.bounty = 3;
    const EnemyDefId runnerId = db.addEnemy(std::move(runner));

    WaveRules rules = db.waves();
    // Wave one: two grunts then three runners, in that order.
    WaveDef first;
    first.enemies = {EnemyDefId{0}, runnerId};
    first.counts = {2, 3};
    rules.waves.push_back(std::move(first));
    rules.endless.healthGrowth = 2.0f;  // exaggerated so the scaling is visible
    db.setWaves(std::move(rules));

    // Enough lives to survive an undefended wave, so the run reaches wave two
    // rather than ending in defeat partway through the thing being tested.
    LevelDef level = db.level();
    level.startingLives = 40;
    db.setLevel(std::move(level));

    World world(db, 99);
    advance(world, 60);
    CHECK_EQ(world.waveNumber(), 1);

    // The authored composition, in the authored order, unscaled.
    std::vector<EnemyDefId> arrivals;
    std::vector<float> healths;
    for (int i = 0; i < 5; ++i) {
        advance(world, 1);
        CHECK_EQ(static_cast<int>(world.enemies().size()), i + 1);
        arrivals.push_back(world.enemies()[static_cast<std::size_t>(i)].type);
        healths.push_back(world.enemies()[static_cast<std::size_t>(i)].maxHealth);
        advance(world, 29);
    }
    CHECK_EQ(arrivals.size(), std::size_t{5});
    CHECK_EQ(arrivals[0], EnemyDefId{0});
    CHECK_EQ(arrivals[1], EnemyDefId{0});
    CHECK_EQ(arrivals[2], runnerId);
    CHECK_EQ(arrivals[4], runnerId);
    CHECK_EQ(bitsOf(healths[0]), bitsOf(30.0f));  // unscaled
    CHECK_EQ(bitsOf(healths[2]), bitsOf(8.0f));

    // Mixed speeds in one wave are what make two enemies able to share a path
    // distance at all -- the shipped level's single-speed lane never can. That
    // was known-wrong #9.
    CHECK(db.enemy(runnerId).speed > db.enemy(0).speed);

    // The first endless wave is the endless rule's wave one, so it is not
    // scaled either; the second is scaled once.
    while (world.waveNumber() < 2 && world.state() == GameState::Playing) {
        world.step();
    }
    CHECK_EQ(world.waveNumber(), 2);
    advance(world, 1);
    CHECK_EQ(bitsOf(world.enemies()[0].maxHealth), bitsOf(30.0f));
}

// The property hot reload rests on.
//
// Pressing F5 rebuilds the world and replays the recorded command log from tick
// zero against the new database. That is only meaningful if replaying against
// the SAME database reproduces the run exactly -- otherwise the designer cannot
// tell which of the differences they are looking at came from their edit and
// which came from the replay itself.
//
// The replay loop lives in app/ because it is driven by a keypress, but the
// guarantee is the simulation's, and this is where it is pinned.
TEST(replaying_a_command_log_reproduces_the_run) {
    const ContentDb db = builtInCode();

    struct Command {
        std::uint64_t tick;
        Cell cell;
        TowerDefId tower;
    };
    static constexpr Command kLog[] = {
        {5, {3, 1}, 0},
        {5, {3, 3}, 1},
        {200, {6, 1}, 1},
        {640, {8, 3}, 0},
    };
    constexpr std::uint64_t kTicks = 1500;

    const auto play = [&] {
        World world(db, 20260824);
        std::size_t next = 0;
        for (std::uint64_t tick = 0; tick < kTicks; ++tick) {
            while (next < std::size(kLog) && kLog[next].tick == tick) {
                (void)world.placeTower(kLog[next].cell, kLog[next].tower);
                ++next;
            }
            world.step();
        }
        return world.checksum();
    };

    // Two placements land on the same tick, which the live loop allows across
    // consecutive frames; the replay has to apply them in the recorded order.
    CHECK_EQ(play(), play());

    // And a rebalanced database must produce a different run, or the reload
    // would show the designer nothing.
    ContentDb rebalanced = builtInCode();
    TowerDef stronger = rebalanced.tower(0);
    stronger.damage *= 3.0f;
    ContentDb tweaked;
    tweaked.addProjectile(rebalanced.projectile(0));
    tweaked.addTower(std::move(stronger));
    tweaked.addTower(rebalanced.tower(1));
    tweaked.addEnemy(rebalanced.enemy(0));
    tweaked.setLevel(rebalanced.level());
    tweaked.setWaves(rebalanced.waves());

    World replayed(tweaked, 20260824);
    std::size_t next = 0;
    for (std::uint64_t tick = 0; tick < kTicks; ++tick) {
        while (next < std::size(kLog) && kLog[next].tick == tick) {
            (void)replayed.placeTower(kLog[next].cell, kLog[next].tower);
            ++next;
        }
        replayed.step();
    }
    CHECK(replayed.checksum() != play());
}
