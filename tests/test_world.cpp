#include "TestFramework.h"

#include "game/World.h"

using namespace ktd;

namespace {

void advance(World& world, float seconds) {
    const int steps = static_cast<int>(seconds / config::kFixedDt);
    for (int i = 0; i < steps; ++i) {
        world.step(config::kFixedDt);
    }
}

}  // namespace

TEST(world_starts_in_a_playable_state) {
    World world;
    CHECK_EQ(world.gold(), config::kStartingGold);
    CHECK_EQ(world.lives(), config::kStartingLives);
    CHECK_EQ(world.waveNumber(), 0);
    CHECK(world.state() == GameState::Playing);
    CHECK(world.enemies().empty());
    CHECK(world.towers().empty());

    // The default level must be solvable, or nothing else can be tested.
    CHECK(pathExists(world.grid(), world.spawn(), world.goal()));
}

TEST(the_first_wave_starts_and_spawns_enemies) {
    World world;
    advance(world, config::kTimeBetweenWaves + 2.0f);

    CHECK_EQ(world.waveNumber(), 1);
    CHECK(!world.enemies().empty());
}

TEST(undefended_enemies_reach_the_goal_and_cost_lives) {
    World world;
    advance(world, 60.0f);

    CHECK(world.enemiesLeaked() > 0);
    CHECK(world.lives() < config::kStartingLives);
    CHECK_EQ(world.enemiesKilled(), 0);  // nothing was built
}

TEST(enough_lost_lives_ends_the_run) {
    World world;
    advance(world, 400.0f);

    CHECK_EQ(world.lives(), 0);
    CHECK(world.state() == GameState::Defeat);

    // A finished run must stop simulating.
    const int leakedAtDefeat = world.enemiesLeaked();
    advance(world, 10.0f);
    CHECK_EQ(world.enemiesLeaked(), leakedAtDefeat);
}

TEST(placing_a_tower_costs_gold_and_occupies_the_tile) {
    World world;
    const Cell cell{2, world.grid().height() / 2 - 1};

    CHECK(world.placeTower(cell) == PlacementResult::Placed);
    CHECK_EQ(world.gold(), config::kStartingGold - config::kTowerCost);
    CHECK_EQ(static_cast<int>(world.towers().size()), 1);
    CHECK(world.grid().at(cell) == Tile::Tower);

    // The same tile cannot be built on twice.
    CHECK(world.placeTower(cell) == PlacementResult::BlockedTile);
}

TEST(placement_fails_without_enough_gold) {
    World world;
    const int affordable = config::kStartingGold / config::kTowerCost;

    int placed = 0;
    for (int y = 0; y < world.grid().height() && placed < affordable; ++y) {
        if (world.placeTower({2, y}) == PlacementResult::Placed) {
            ++placed;
        }
    }
    CHECK_EQ(placed, affordable);
    CHECK(world.gold() < config::kTowerCost);
    CHECK(world.placeTower({4, 1}) == PlacementResult::NotEnoughGold);
}

TEST(towers_kill_enemies_and_pay_out_gold) {
    World world;

    // Build a firing line along the corridor the enemies must use.
    const int lane = world.grid().height() / 2;
    for (int x = 2; x <= 6; ++x) {
        world.placeTower({x, lane - 1});
        world.placeTower({x, lane + 1});
    }
    CHECK(!world.towers().empty());

    const int goldAfterBuilding = world.gold();
    advance(world, 45.0f);

    CHECK(world.enemiesKilled() > 0);
    CHECK(world.gold() > goldAfterBuilding);  // kills paid out

    // The property that actually matters: a defended lane leaks less than an
    // undefended one over the same span. Asserting on projectiles in flight
    // would only test where the loop happened to stop.
    World undefended;
    advance(undefended, 45.0f);
    CHECK(world.enemiesLeaked() < undefended.enemiesLeaked());
    CHECK(world.lives() > undefended.lives());
}

TEST(the_route_stays_open_no_matter_what_the_player_builds) {
    World world;

    // Try to build on every tile on the board; the placement rule must never
    // let the player seal the route off.
    for (int y = 0; y < world.grid().height(); ++y) {
        for (int x = 0; x < world.grid().width(); ++x) {
            world.placeTower({x, y});
            CHECK(pathExists(world.grid(), world.spawn(), world.goal()));
        }
    }
}

TEST(identical_seeds_produce_identical_runs) {
    // The property that makes replays, save/resume and cross-platform
    // consistency possible. If this ever fails, something in the simulation
    // has started depending on non-deterministic state.
    World a(config::kGridWidth, config::kGridHeight, 4242);
    World b(config::kGridWidth, config::kGridHeight, 4242);

    const int lane = config::kGridHeight / 2;
    for (int x = 3; x <= 5; ++x) {
        a.placeTower({x, lane - 1});
        b.placeTower({x, lane - 1});
    }

    advance(a, 40.0f);
    advance(b, 40.0f);

    CHECK_EQ(a.gold(), b.gold());
    CHECK_EQ(a.lives(), b.lives());
    CHECK_EQ(a.enemiesKilled(), b.enemiesKilled());
    CHECK_EQ(a.waveNumber(), b.waveNumber());
    CHECK_EQ(static_cast<int>(a.enemies().size()), static_cast<int>(b.enemies().size()));

    for (std::size_t i = 0; i < a.enemies().size(); ++i) {
        CHECK_NEAR(a.enemies()[i].position.x, b.enemies()[i].position.x, 1e-6);
        CHECK_NEAR(a.enemies()[i].position.y, b.enemies()[i].position.y, 1e-6);
        CHECK_NEAR(a.enemies()[i].health, b.enemies()[i].health, 1e-6);
    }
}

TEST(different_seeds_still_produce_a_valid_run) {
    World world(config::kGridWidth, config::kGridHeight, 777);
    advance(world, 30.0f);
    CHECK(world.waveNumber() > 0);
    CHECK(world.lives() <= config::kStartingLives);
}
