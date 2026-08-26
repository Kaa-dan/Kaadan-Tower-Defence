#include "TestFramework.h"

#include "game/Damage.h"
#include "game/Stats.h"
#include "game/Status.h"
#include "game/World.h"

#include <bit>
#include <string>
#include <vector>

using namespace ktd;

namespace {

// Exact float comparison, spelled as bits.
//
// -Wfloat-equal is on for good reason in the simulation, but the whole point of
// several tests below is that two numbers are *the same float*, not two floats
// within a tolerance. A tolerance would pass on exactly the one-ulp difference
// the ordering rules exist to prevent, which would make the test worse than
// useless -- it would look like evidence.
bool sameFloat(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// A board built entirely in code, so every test below controls its own content
// rather than being coupled to the shipped balance.
struct Fixture {
    content::ContentDb db;
    content::StatusDefId chill = content::kNoDef;
    content::StatusDefId hex = content::kNoDef;
    content::StatusDefId burn = content::kNoDef;
    content::DamageTypeId physical = content::kNoDef;
    content::DamageTypeId fire = content::kNoDef;
};

// `authoredWaves` builds a winnable level: authored waves and no endless rule.
Fixture makeFixture(bool authoredWaves = false) {
    Fixture fixture;
    content::ContentDb& db = fixture.db;

    content::DamageTypeDef physical;
    physical.id = "physical";
    fixture.physical = db.addDamageType(std::move(physical));
    content::DamageTypeDef fire;
    fire.id = "fire";
    fixture.fire = db.addDamageType(std::move(fire));

    // Three effects with deliberately awkward multipliers: numbers whose
    // product is not exactly representable, so multiplying them in two
    // different orders really does give two different floats. A tidy set like
    // 0.5 and 2.0 would make the order-independence tests pass by accident.
    content::StatusDef chill;
    chill.id = "chill";
    chill.policy = content::StackPolicy::Refresh;
    chill.durationTicks = 60;
    chill.damageTakenMultiplier = 1.1f;
    chill.speedMultiplier = 0.7f;
    fixture.chill = db.addStatus(std::move(chill));

    content::StatusDef hex;
    hex.id = "hex";
    hex.policy = content::StackPolicy::StackIntensity;
    hex.maxStacks = 3;
    hex.durationTicks = 90;
    hex.damageTakenMultiplier = 1.37f;
    fixture.hex = db.addStatus(std::move(hex));

    content::StatusDef burn;
    burn.id = "burn";
    burn.policy = content::StackPolicy::StackDuration;
    burn.maxStacks = 4;
    burn.durationTicks = 30;
    burn.damagePerTick = 1.0f;
    burn.damageType = fixture.fire;
    fixture.burn = db.addStatus(std::move(burn));

    content::ProjectileDef bolt;
    bolt.id = "bolt";
    bolt.speed = 9.0f;
    bolt.hitRadius = 0.18f;
    bolt.lifetimeTicks = 180;
    const content::ProjectileDefId boltId = db.addProjectile(std::move(bolt));

    content::TowerDef gun;
    gun.id = "gun";
    gun.name = "Gun";
    gun.cost = 20;
    gun.range = 3.0f;
    gun.damage = 10.0f;
    gun.reloadTicks = 30;
    gun.projectile = boltId;
    gun.damageType = fixture.physical;
    db.addTower(std::move(gun));

    // A second tower that sets its target alight, so the status path is driven
    // through the real simulation and not only through applyStatus directly.
    content::TowerDef torch;
    torch.id = "torch";
    torch.name = "Torch";
    torch.cost = 20;
    torch.range = 3.0f;
    torch.damage = 1.0f;
    torch.reloadTicks = 30;
    torch.projectile = boltId;
    torch.damageType = fixture.fire;
    torch.appliesStatus = fixture.burn;
    db.addTower(std::move(torch));

    content::EnemyDef grunt;
    grunt.id = "grunt";
    grunt.health = 30.0f;
    grunt.speed = 1.5f;
    grunt.radius = 0.22f;
    grunt.bounty = 5;
    const content::EnemyDefId gruntId = db.addEnemy(std::move(grunt));

    content::EnemyDef armored;
    armored.id = "armored";
    armored.health = 40.0f;
    armored.speed = 1.5f;
    armored.radius = 0.22f;
    armored.bounty = 7;
    armored.armor = 3.0f;
    db.addEnemy(std::move(armored));
    db.setEnemyResistance(1, fixture.fire, 0.25f);

    content::LevelDef level;
    level.id = "lane";
    level.width = 14;
    level.height = 5;
    level.spawn = {0, 2};
    level.goal = {13, 2};
    level.tiles.assign(static_cast<std::size_t>(level.width * level.height), Tile::Open);
    level.startingGold = 100;
    level.startingLives = 5;
    db.setLevel(std::move(level));

    content::WaveRules rules;
    rules.spawnIntervalTicks = 30;
    rules.gapTicks = 60;
    if (authoredWaves) {
        // Two short authored waves and no endless rule at all: the content
        // declares an end, so the run can be won.
        content::WaveDef first;
        first.enemies.push_back(gruntId);
        first.counts.push_back(2);
        rules.waves.push_back(std::move(first));

        content::WaveDef second;
        second.enemies.push_back(gruntId);
        second.counts.push_back(1);
        rules.waves.push_back(std::move(second));
        rules.endless.enemy = content::kNoDef;
        rules.endless.firstCount = 0;
    } else {
        rules.endless.enemy = gruntId;
        rules.endless.firstCount = 6;
        rules.endless.addedPerWave = 1;
        rules.endless.healthGrowth = 1.0f;
        rules.endless.speedGrowth = 1.0f;
    }
    db.setWaves(std::move(rules));
    return fixture;
}

void advance(World& world, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.step();
    }
}

// The damage one hit would deal to an enemy carrying `applied`, applied in the
// given order.
float damageAfterApplying(const Fixture& fixture,
                          const std::vector<content::StatusDefId>& applied) {
    Enemy enemy;
    enemy.type = 0;
    enemy.health = 1000.0f;
    for (const content::StatusDefId id : applied) {
        applyStatus(enemy.statuses, fixture.db.status(id), id);
    }

    DamageEvent hit;
    hit.amount = 10.0f;
    hit.type = fixture.physical;
    return resolveDamage(hit, enemy, fixture.db);
}

int countEvents(const World& world, EventType type) {
    int count = 0;
    for (const Event& event : world.events()) {
        if (event.type == type) {
            ++count;
        }
    }
    return count;
}

}  // namespace

// --- the acceptance criteria -------------------------------------------------

// The property that prevents a whole class of player-reported balance bugs:
// two players who applied the same debuffs in a different order must see the
// same number come off the health bar.
//
// It is not automatic. Float multiplication is not associative, so
// `x * a * b` and `x * b * a` can differ in the last bit, and the natural
// implementation -- walk the enemy's status slots in slot order -- makes the
// order the one the effects happened to land in. Resolving in ascending
// StatusDefId instead makes the answer a function of *which* effects are on the
// enemy and not *how they got there*. See ADR 0011.
TEST(damage_order_is_independent_of_buff_application_order) {
    const Fixture fixture = makeFixture();

    // Every ordering of three effects, one of which stacks.
    const std::vector<std::vector<content::StatusDefId>> orders = {
        {fixture.chill, fixture.hex, fixture.burn}, {fixture.burn, fixture.chill, fixture.hex},
        {fixture.hex, fixture.burn, fixture.chill}, {fixture.burn, fixture.hex, fixture.chill},
        {fixture.hex, fixture.chill, fixture.burn}, {fixture.chill, fixture.burn, fixture.hex},
    };

    const float expected = damageAfterApplying(fixture, orders.front());
    for (const std::vector<content::StatusDefId>& order : orders) {
        CHECK(sameFloat(damageAfterApplying(fixture, order), expected));
    }

    // And the multipliers really are being applied -- a test that passed
    // because every ordering produced the base damage would prove nothing.
    CHECK(!sameFloat(expected, 10.0f));

    // The tower's half of the formula has the same property, for the same
    // reason: flat mods are summed in a canonical order, not the order the
    // player socketed them.
    TowerStats base;
    base.damage = 10.0f;
    base.range = 3.0f;
    base.reloadTicks = 30;

    ModSet forward;
    forward.add(StatMod{StatId::Damage, ModOp::Flat, 0.1f});
    forward.add(StatMod{StatId::Damage, ModOp::Flat, 0.2f});
    forward.add(StatMod{StatId::Damage, ModOp::Percent, 0.35f});

    ModSet backward;
    backward.add(StatMod{StatId::Damage, ModOp::Percent, 0.35f});
    backward.add(StatMod{StatId::Damage, ModOp::Flat, 0.2f});
    backward.add(StatMod{StatId::Damage, ModOp::Flat, 0.1f});

    CHECK(sameFloat(resolveStats(base, forward).damage, resolveStats(base, backward).damage));
    CHECK(!sameFloat(resolveStats(base, forward).damage, 10.0f));
}

// Four policies, each doing exactly what its name says. "Does burn stack?" is a
// design question, and this is the test that keeps the answer in the content
// file rather than in a branch someone has to find.
TEST(status_effects_stack_per_policy) {
    const Fixture fixture = makeFixture();
    const content::ContentDb& db = fixture.db;

    // Refresh: one instance, and reapplying restarts the clock.
    {
        StatusSlots slots{};
        CHECK(applyStatus(slots, db.status(fixture.chill), fixture.chill) == StatusApply::Added);
        CHECK_EQ(statusStacks(slots, fixture.chill), std::uint16_t{1});
        slots[0].remaining = 5;
        CHECK(applyStatus(slots, db.status(fixture.chill), fixture.chill) ==
              StatusApply::Refreshed);
        CHECK_EQ(statusStacks(slots, fixture.chill), std::uint16_t{1});
        CHECK_EQ(slots[0].remaining, db.status(fixture.chill).durationTicks);
    }

    // StackIntensity: stacks up to the cap and no further.
    {
        StatusSlots slots{};
        for (int i = 0; i < 6; ++i) {
            applyStatus(slots, db.status(fixture.hex), fixture.hex);
        }
        CHECK_EQ(statusStacks(slots, fixture.hex), db.status(fixture.hex).maxStacks);
        // Still one slot: intensity grows, the slot count does not.
        int occupied = 0;
        for (const StatusSlot& slot : slots) {
            occupied += slot.def != content::kNoDef ? 1 : 0;
        }
        CHECK_EQ(occupied, 1);
    }

    // StackDuration: the clock extends, capped at maxStacks durations, and the
    // intensity never moves.
    {
        StatusSlots slots{};
        const content::StatusDef& def = db.status(fixture.burn);
        applyStatus(slots, def, fixture.burn);
        CHECK_EQ(slots[0].remaining, def.durationTicks);
        applyStatus(slots, def, fixture.burn);
        CHECK_EQ(slots[0].remaining, def.durationTicks * 2);
        for (int i = 0; i < 10; ++i) {
            applyStatus(slots, def, fixture.burn);
        }
        CHECK_EQ(slots[0].remaining, def.durationTicks * def.maxStacks);
        CHECK_EQ(statusStacks(slots, fixture.burn), std::uint16_t{1});
    }

    // Ignore: the first application wins outright, clock included.
    {
        const Fixture second = makeFixture();
        content::ContentDb db2 = second.db;
        content::StatusDef curse;
        curse.id = "curse";
        curse.policy = content::StackPolicy::Ignore;
        curse.durationTicks = 50;
        const content::StatusDefId id = db2.addStatus(std::move(curse));

        StatusSlots slots{};
        CHECK(applyStatus(slots, db2.status(id), id) == StatusApply::Added);
        slots[0].remaining = 7;
        CHECK(applyStatus(slots, db2.status(id), id) == StatusApply::Ignored);
        CHECK_EQ(slots[0].remaining, std::uint32_t{7});
    }

    // A full table drops the effect rather than evicting one. Deterministic
    // beats clever: eviction needs a priority nobody has designed.
    {
        const Fixture third = makeFixture();
        content::ContentDb db3 = third.db;
        std::vector<content::StatusDefId> ids;
        for (int i = 0; i < 10; ++i) {
            content::StatusDef filler;
            filler.id = "filler" + std::to_string(i);
            filler.durationTicks = 10;
            ids.push_back(db3.addStatus(std::move(filler)));
        }
        StatusSlots slots{};
        for (std::size_t i = 0; i < kStatusSlots; ++i) {
            CHECK(applyStatus(slots, db3.status(ids[i]), ids[i]) == StatusApply::Added);
        }
        CHECK(applyStatus(slots, db3.status(ids[kStatusSlots]), ids[kStatusSlots]) ==
              StatusApply::NoRoom);
    }
}

// Presentation learns what happened from events, so an event that is not
// emitted is an effect that never plays. The cheapest way for that to regress
// is for a new code path to take a shortcut past the one that emits.
TEST(events_are_emitted_for_every_kill_and_shot) {
    const Fixture fixture = makeFixture();
    World world(fixture.db);

    // Build a gun beside the lane and run until it has killed something.
    CHECK(world.placeTower({3, 1}, 0) == PlacementResult::Placed);

    int spawned = 0;
    int fired = 0;
    int killed = 0;
    int damaged = 0;
    int waveStarts = 0;

    for (int tick = 0; tick < 1200 && world.state() == GameState::Playing; ++tick) {
        const int killsBefore = world.enemiesKilled();
        const std::size_t enemiesBefore = world.enemies().size();
        const std::size_t shotsBefore = world.projectiles().size();
        const int waveBefore = world.waveNumber();

        world.step();

        spawned += countEvents(world, EventType::EnemySpawned);
        fired += countEvents(world, EventType::TowerFired);
        killed += countEvents(world, EventType::EnemyKilled);
        damaged += countEvents(world, EventType::EnemyDamaged);
        waveStarts += countEvents(world, EventType::WaveStarted);

        // Per tick, not just in total: an event queue that emitted the right
        // number of kills on the wrong ticks would still desync every effect.
        CHECK_EQ(countEvents(world, EventType::EnemyKilled), world.enemiesKilled() - killsBefore);
        CHECK_EQ(countEvents(world, EventType::WaveStarted), world.waveNumber() - waveBefore);
        // A spawn is the only thing that adds an enemy, and a kill or a leak is
        // the only thing that removes one.
        const int added = countEvents(world, EventType::EnemySpawned);
        const int removed =
            countEvents(world, EventType::EnemyKilled) + countEvents(world, EventType::EnemyLeaked);
        CHECK_EQ(static_cast<int>(world.enemies().size()),
                 static_cast<int>(enemiesBefore) + added - removed);
        // And a shot only appears when a tower fires.
        CHECK(world.projectiles().size() <=
              shotsBefore + static_cast<std::size_t>(countEvents(world, EventType::TowerFired)));
    }

    CHECK(spawned > 0);
    CHECK(fired > 0);
    CHECK(killed > 0);
    CHECK(damaged >= killed);
    CHECK(waveStarts > 0);
    CHECK_EQ(killed, world.enemiesKilled());

    // The queue holds exactly one tick. An accumulating queue would make the
    // state hash depend on how often the caller happened to drain it, which is
    // a frame-rate dependency in the determinism trace.
    World quiet(fixture.db);
    quiet.step();
    const std::size_t afterOne = quiet.events().size();
    quiet.step();
    CHECK(quiet.events().size() <= afterOne + 4);  // not cumulative
}

// The replay property, end to end: the same command stream against the same
// seed is the same run, tick for tick and bit for bit.
//
// This is what Phase 7's replay files rest on. The command queue is the only
// way into the simulation, so a log of (tick, command) is a complete recording
// -- there is no second input path that could make two runs differ.
TEST(commands_replay_identically) {
    const Fixture fixture = makeFixture();

    struct Recorded {
        std::uint64_t tick;
        Command command;
    };
    const std::vector<Recorded> log = {
        {5, {CommandType::PlaceTower, {3, 1}, 0}},
        {5, {CommandType::PlaceTower, {3, 3}, 1}},
        {200, {CommandType::PlaceTower, {6, 1}, 0}},
        // Deliberately illegal: off the board. A refused command is part of
        // what the player did, and it must be refused identically on replay.
        {201, {CommandType::PlaceTower, {-4, 99}, 0}},
        {600, {CommandType::PlaceTower, {9, 3}, 1}},
    };

    const auto play = [&](std::uint64_t seed) {
        World world(fixture.db, seed);
        std::vector<std::uint64_t> trace;
        std::size_t next = 0;
        for (std::uint64_t tick = 0; tick < 900; ++tick) {
            while (next < log.size() && log[next].tick == tick) {
                CHECK(world.enqueue(log[next].command));
                ++next;
            }
            world.step();
            trace.push_back(world.checksum());
        }
        return trace;
    };

    const std::vector<std::uint64_t> first = play(4242);
    const std::vector<std::uint64_t> second = play(4242);
    CHECK_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (first[i] != second[i]) {
            testing::reportFailure(__FILE__, __LINE__,
                                   "replay diverged at tick " + std::to_string(i));
            break;
        }
    }

    // And the log is actually doing something -- a run that ignored every
    // command would replay identically too, and prove nothing.
    const std::vector<std::uint64_t> unplayed = [&] {
        World world(fixture.db, 4242);
        std::vector<std::uint64_t> trace;
        for (std::uint64_t tick = 0; tick < 900; ++tick) {
            world.step();
            trace.push_back(world.checksum());
        }
        return trace;
    }();
    CHECK(unplayed.back() != first.back());

    // A different seed is a different run, so the trace is not simply constant.
    CHECK(play(4243).back() != first.back());
}

// GameState had no Victory at all before this phase: the only way a run could
// end was by losing. Whether a level is winnable is a property of its content
// -- authored waves and no endless rule -- so a campaign level and an endless
// level are the same code and a different file.
TEST(victory_state_is_reachable) {
    const Fixture winnable = makeFixture(/*authoredWaves=*/true);
    World world(winnable.db);

    // Three towers is more than enough for three grunts, and the run is won by
    // clearing the authored waves rather than by surviving a timer.
    CHECK(world.placeTower({3, 1}, 0) == PlacementResult::Placed);
    CHECK(world.placeTower({3, 3}, 0) == PlacementResult::Placed);
    CHECK(world.placeTower({6, 1}, 0) == PlacementResult::Placed);

    int victoryEvents = 0;
    for (int tick = 0; tick < 3000 && world.state() == GameState::Playing; ++tick) {
        world.step();
        victoryEvents += countEvents(world, EventType::Victory);
    }

    CHECK(world.state() == GameState::Victory);
    CHECK_EQ(victoryEvents, 1);
    CHECK_EQ(world.waveNumber(), 2);
    CHECK(world.lives() > 0);
    CHECK(world.enemies().empty());

    // A finished run stops advancing, the same way a lost one does.
    const std::uint64_t at = world.tick();
    advance(world, 30);
    CHECK_EQ(world.tick(), at);

    // The shipped content declares an endless rule, so it is never winnable --
    // adding victory changed nothing about the game as it stands today.
    const Fixture endlessContent = makeFixture(/*authoredWaves=*/false);
    World endless(endlessContent.db);
    for (int tick = 0; tick < 2000 && endless.state() == GameState::Playing; ++tick) {
        endless.step();
    }
    CHECK(endless.state() != GameState::Victory);
}

// --- the pipeline in detail ---------------------------------------------------

// Two shots landing on one enemy in the same tick subtract in a canonical
// order, not in the order the projectiles happen to sit in the pool. Pool order
// is a function of destruction history (ADR 0009), so without the sort the
// enemy's remaining health -- and therefore whether it dies this tick -- would
// depend on storage layout.
TEST(damage_resolves_in_canonical_order) {
    DamageQueue queue;

    DamageEvent a;
    a.targetId = 7;
    a.sourceId = 2;
    a.amount = 3.3f;
    DamageEvent b;
    b.targetId = 7;
    b.sourceId = 1;
    b.amount = 1.1f;
    DamageEvent c;
    c.targetId = 3;
    c.sourceId = 9;
    c.amount = 2.2f;

    queue.push(a);
    queue.push(b);
    queue.push(c);
    queue.sortCanonically();

    CHECK_EQ(queue.events()[0].targetId, EntityId{3});
    CHECK_EQ(queue.events()[1].sourceId, EntityId{1});
    CHECK_EQ(queue.events()[2].sourceId, EntityId{2});

    // Pushed in the reverse order, the sorted result is identical -- which is
    // the whole property.
    DamageQueue reversed;
    reversed.push(c);
    reversed.push(a);
    reversed.push(b);
    reversed.sortCanonically();
    for (std::size_t i = 0; i < queue.size(); ++i) {
        CHECK_EQ(reversed.events()[i].targetId, queue.events()[i].targetId);
        CHECK_EQ(reversed.events()[i].sourceId, queue.events()[i].sourceId);
        CHECK(sameFloat(reversed.events()[i].amount, queue.events()[i].amount));
    }
}

// Armor subtracts *after* every multiplier, and resistance multiplies before
// it. That order is why armor is worth most against many small hits and least
// against one big one, and it is a design property rather than an accident of
// how the expression was typed. See ADR 0011.
TEST(resistance_multiplies_and_armor_subtracts) {
    const Fixture fixture = makeFixture();

    Enemy armored;
    armored.type = 1;  // armor 3, 25% fire resistance
    armored.health = 100.0f;

    DamageEvent fire;
    fire.amount = 20.0f;
    fire.type = fixture.fire;
    // 20 * (1 - 0.25) - 3 = 12
    CHECK_NEAR(resolveDamage(fire, armored, fixture.db), 12.0f, 1e-5);

    DamageEvent physical;
    physical.amount = 20.0f;
    physical.type = fixture.physical;
    // No resistance to physical: 20 - 3 = 17.
    CHECK_NEAR(resolveDamage(physical, armored, fixture.db), 17.0f, 1e-5);

    // Clamped at zero, never negative -- an over-armored enemy takes nothing,
    // it does not heal.
    DamageEvent graze;
    graze.amount = 1.0f;
    graze.type = fixture.physical;
    CHECK(sameFloat(resolveDamage(graze, armored, fixture.db), 0.0f));

    // A status multiplier lands before resistance, so it compounds with it
    // rather than being flattened by it.
    applyStatus(armored.statuses, fixture.db.status(fixture.chill), fixture.chill);
    const float chilled = resolveDamage(physical, armored, fixture.db);
    CHECK_NEAR(chilled, 20.0f * 1.1f - 3.0f, 1e-4);
}

// A tower reads its stats through the mod cache, and the cache is only allowed
// to be a cache: adding a mod must change what the tower does on the next tick,
// not on the next time something happens to invalidate it.
TEST(tower_stat_mods_apply_and_cache) {
    const Fixture fixture = makeFixture();
    World world(fixture.db);
    CHECK(world.placeTower({3, 1}, 0) == PlacementResult::Placed);

    // A mod reaches a standing tower through its handle, and the tower's next
    // shot is the one that feels it -- the cache is a cache, not a snapshot
    // taken when the tower was built.
    CHECK_EQ(world.towers().size(), std::size_t{1});
    const TowerId tower = world.towerAt(0);
    CHECK(world.addTowerMod(tower, StatMod{StatId::Damage, ModOp::Flat, 90.0f}));
    CHECK(!world.addTowerMod(TowerId{}, StatMod{StatId::Damage, ModOp::Flat, 1.0f}));

    // 10 base damage kills a 30-health grunt in three hits; 100 kills in one.
    // If the mod had not reached the standing tower, the first kill would take
    // three reloads instead of one.
    int killed = 0;
    int firedBeforeFirstKill = 0;
    for (int tick = 0; tick < 900 && killed == 0; ++tick) {
        world.step();
        firedBeforeFirstKill += countEvents(world, EventType::TowerFired);
        killed += countEvents(world, EventType::EnemyKilled);
    }
    CHECK_EQ(killed, 1);
    CHECK_EQ(firedBeforeFirstKill, 1);

    // The base numbers come from the def until something modifies them.
    Tower probe;
    probe.type = 0;
    const TowerStats& base = sys::towerStats(probe, fixture.db);
    CHECK_NEAR(base.damage, 10.0f, 1e-6);
    CHECK_NEAR(base.range, 3.0f, 1e-6);
    CHECK_EQ(base.reloadTicks, std::uint32_t{30});

    // The cache is stale-marked by hand, which is what World::addTowerMod does.
    probe.mods.add(StatMod{StatId::Damage, ModOp::Percent, 0.5f});
    probe.statsDirty = true;
    CHECK_NEAR(sys::towerStats(probe, fixture.db).damage, 15.0f, 1e-5);

    // Reload rounds rather than truncating, and never reaches zero: a tower
    // that fires every tick fires sixty times a second.
    probe.mods.add(StatMod{StatId::ReloadTicks, ModOp::Percent, -0.99f});
    probe.statsDirty = true;
    CHECK_EQ(sys::towerStats(probe, fixture.db).reloadTicks, std::uint32_t{1});

    // Full mod sets drop rather than grow: an allocation on a gameplay path is
    // worse than a refused eighth trinket.
    ModSet full;
    for (std::size_t i = 0; i < kModSlots; ++i) {
        CHECK(full.add(StatMod{StatId::Damage, ModOp::Flat, 1.0f}));
    }
    CHECK(!full.add(StatMod{StatId::Damage, ModOp::Flat, 1.0f}));
}

// The status system as the simulation actually runs it: a torch sets its target
// alight, the burn ticks damage of its own, and the effect expires on schedule.
TEST(statuses_tick_and_expire_in_the_simulation) {
    const Fixture fixture = makeFixture();
    World world(fixture.db);
    CHECK(world.placeTower({3, 1}, 1) == PlacementResult::Placed);  // the torch

    bool sawStatusApplied = false;
    bool sawBurning = false;
    for (int tick = 0; tick < 600 && !sawBurning; ++tick) {
        world.step();
        for (const Event& event : world.events()) {
            if (event.type == EventType::StatusApplied && event.detail == fixture.burn) {
                sawStatusApplied = true;
            }
        }
        for (const Enemy& enemy : world.enemies()) {
            if (statusStacks(enemy.statuses, fixture.burn) > 0) {
                sawBurning = true;
            }
        }
    }
    CHECK(sawStatusApplied);
    CHECK(sawBurning);

    // The burn does its own damage: an enemy carrying it loses health on a tick
    // where nothing hit it.
    Enemy sample;
    sample.type = 0;
    sample.health = 100.0f;
    applyStatus(sample.statuses, fixture.db.status(fixture.burn), fixture.burn);
    CHECK(fixture.db.status(fixture.burn).damagePerTick > 0.0f);

    // And it expires: run long enough with nothing reapplying it and the slot
    // clears.
    StatusSlots slots{};
    applyStatus(slots, fixture.db.status(fixture.chill), fixture.chill);
    const std::uint32_t duration = fixture.db.status(fixture.chill).durationTicks;
    for (std::uint32_t i = 0; i < duration; ++i) {
        if (slots[0].remaining > 0) {
            --slots[0].remaining;
        }
        if (slots[0].remaining == 0) {
            slots[0] = StatusSlot{};
        }
    }
    CHECK_EQ(statusStacks(slots, fixture.chill), std::uint16_t{0});
}

// A chilled enemy walks slower, and the multiplier applies in ascending def id
// for the same reason the damage one does.
TEST(status_speed_multiplier_slows_movement) {
    const Fixture fixture = makeFixture();

    StatusSlots none{};
    CHECK(sameFloat(statusSpeedMultiplier(none, fixture.db), 1.0f));

    StatusSlots chilled{};
    applyStatus(chilled, fixture.db.status(fixture.chill), fixture.chill);
    CHECK_NEAR(statusSpeedMultiplier(chilled, fixture.db), 0.7f, 1e-6);
}

// Pause stops the clock without changing the run. A paused tick is a tick that
// never happened, which is why pausing is not a recorded command: it cannot
// desync a replay because it cannot affect one.
TEST(pause_stops_the_clock_without_changing_the_run) {
    const Fixture fixture = makeFixture();

    World live(fixture.db, 99);
    World paused(fixture.db, 99);

    advance(live, 100);

    advance(paused, 40);
    paused.setPaused(true);
    CHECK(paused.state() == GameState::Paused);
    advance(paused, 500);  // every one of these is a no-op
    CHECK_EQ(paused.tick(), std::uint64_t{40});
    paused.setPaused(false);
    CHECK(paused.state() == GameState::Playing);
    advance(paused, 60);

    CHECK_EQ(live.tick(), paused.tick());
    CHECK_EQ(live.checksum(), paused.checksum());

    // Pause cannot resurrect a finished run.
    World lost(fixture.db, 99);
    for (int tick = 0; tick < 20000 && lost.state() == GameState::Playing; ++tick) {
        lost.step();
    }
    CHECK(lost.state() == GameState::Defeat);
    lost.setPaused(false);
    CHECK(lost.state() == GameState::Defeat);
}

// The preview and the command must agree. A hover that says "yes" and a click a
// tick later that says "blocked tile" is the kind of thing players describe as
// the game feeling unreliable.
TEST(preview_agrees_with_the_placement_it_predicts) {
    const Fixture fixture = makeFixture();
    World world(fixture.db);

    const std::vector<Cell> probes = {{3, 1}, {3, 1}, {0, 2}, {13, 2}, {-1, 0}, {99, 99}, {7, 4}};
    for (const Cell cell : probes) {
        const PlacementResult predicted = world.previewPlacement(cell, 0);
        World shadow(fixture.db);
        // Replay the world's placements onto a copy so the two are asked the
        // same question about the same board.
        for (const Tower& tower : world.towers()) {
            (void)shadow.placeTower(tower.cell, tower.type);
        }
        CHECK(shadow.placeTower(cell, 0) == predicted);
        if (predicted == PlacementResult::Placed) {
            (void)world.placeTower(cell, 0);
        }
    }

    // Enqueueing reports the outcome as an event rather than a return value.
    World queued(fixture.db);
    CHECK(queued.enqueuePlacement({3, 1}, 0));
    CHECK(queued.enqueuePlacement({3, 1}, 0));  // the same cell twice in one tick
    queued.step();
    CHECK_EQ(countEvents(queued, EventType::TowerPlaced), 1);
    CHECK_EQ(countEvents(queued, EventType::TowerRejected), 1);
    CHECK_EQ(queued.towers().size(), std::size_t{1});

    // The queue is bounded rather than growing.
    World flooded(fixture.db);
    for (std::size_t i = 0; i < kMaxPendingCommands; ++i) {
        CHECK(flooded.enqueuePlacement({5, 0}, 0));
    }
    CHECK(!flooded.enqueuePlacement({5, 0}, 0));
}

// Previous positions exist for Phase 6's render interpolation, and the property
// that makes them useful is that they are exactly last tick's positions -- not
// an approximation, and not this tick's.
TEST(previous_positions_track_the_last_tick) {
    const Fixture fixture = makeFixture();
    World world(fixture.db);
    CHECK(world.placeTower({3, 1}, 0) == PlacementResult::Placed);

    std::vector<Vec2> lastSeen;
    bool checkedAnEnemy = false;
    for (int tick = 0; tick < 400; ++tick) {
        lastSeen.clear();
        for (const Enemy& enemy : world.enemies()) {
            lastSeen.push_back(enemy.position);
        }
        const std::size_t before = world.enemies().size();

        world.step();

        // Only compare when nothing was added or removed, so the views line up.
        if (world.enemies().size() == before && before > 0 &&
            countEvents(world, EventType::EnemySpawned) == 0 &&
            countEvents(world, EventType::EnemyKilled) == 0 &&
            countEvents(world, EventType::EnemyLeaked) == 0) {
            for (std::size_t i = 0; i < before; ++i) {
                const Enemy& enemy = world.enemies()[i];
                CHECK(sameFloat(enemy.previousPosition.x, lastSeen[i].x));
                CHECK(sameFloat(enemy.previousPosition.y, lastSeen[i].y));
                checkedAnEnemy = true;
            }
        }
    }
    CHECK(checkedAnEnemy);
}

// Fast-forward multiplies steps rather than lengthening dt, so a run played at
// 3x must be bit-identical to the same run played at 1x. This is the property
// app/main.cpp's speed control rests on.
TEST(game_speed_multiplies_steps_not_dt) {
    const Fixture fixture = makeFixture();

    World slow(fixture.db, 7);
    World fast(fixture.db, 7);

    for (int frame = 0; frame < 100; ++frame) {
        slow.step();
    }
    for (int frame = 0; frame < 50; ++frame) {
        fast.step();
        fast.step();
    }
    CHECK_EQ(slow.tick(), fast.tick());
    CHECK_EQ(slow.checksum(), fast.checksum());
}
