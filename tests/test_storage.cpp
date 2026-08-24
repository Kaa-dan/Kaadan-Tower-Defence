#include "TestFramework.h"

#include "game/Pool.h"
#include "game/SpatialIndex.h"
#include "game/Targeting.h"
#include "game/World.h"

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

using namespace ktd;

namespace {

// Program-wide allocation counter.
//
// Replacing the global operator new is heavy -- it affects every test in the
// binary -- but it is the only way to assert "this code path does not allocate"
// as a property rather than as a claim in a comment. The counters are plain
// globals rather than atomics because the test binary is single-threaded, and
// <atomic> is what the determinism guard bans from game/ anyway.
//
// THE RULE THAT MATTERS: every allocating form whose memory can reach one of the
// replaced deletes must itself be replaced. Deletes are not matched to news by
// which overload allocated; ordinary `operator delete(void*)` frees memory from
// the nothrow new as well. Replacing the throwing forms and forgetting the
// nothrow ones therefore routes an ASan-allocated block into std::free, and
// AddressSanitizer correctly reports alloc-dealloc-mismatch.
//
// That is not hypothetical -- it is what broke CI. libstdc++'s std::stable_sort
// takes its merge buffer from std::get_temporary_buffer, which calls
// `operator new(n, std::nothrow)`, and World::serialize sorts entities into
// canonical id order on every checksum(). Every hash test failed on Linux while
// macOS passed, because libc++ does not use nothrow new on that path. A
// standard-library implementation detail, visible only on one platform.
//
// The over-aligned forms (std::align_val_t) are deliberately NOT replaced. They
// pair only with the aligned deletes, which are also left alone, so both halves
// stay with the default implementation and cannot mismatch -- and replacing them
// would need std::aligned_alloc, which MSVC does not provide.
std::size_t g_allocations = 0;

}  // namespace

void* operator new(std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

// The targeting rule, written out independently of the implementation: of every
// enemy within range whose tile can reach the goal, the one closest to the goal,
// ties broken on the lower entity id. If this and World::findTarget ever
// disagree, one of them is wrong -- and the point of restating it here is that
// the test does not inherit the implementation's mistakes.
const Enemy* bruteForceTarget(const World& world, const Tower& tower, int& tiedCount) {
    const Vec2 origin = Grid::centerOf(tower.cell);
    const float rangeSq = tower.range * tower.range;

    const Enemy* best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    tiedCount = 0;

    for (const Enemy& enemy : world.enemies()) {
        if (lengthSq(enemy.position - origin) > rangeSq) {
            continue;
        }
        const int toGoal = world.flowField().distance(Grid::cellOf(enemy.position));
        if (toGoal < 0) {
            continue;
        }
        if (best == nullptr || toGoal < bestDistance) {
            bestDistance = toGoal;
            best = &enemy;
        } else if (toGoal == bestDistance && enemy.id < best->id) {
            best = &enemy;
        }
    }

    if (best != nullptr) {
        for (const Enemy& enemy : world.enemies()) {
            if (lengthSq(enemy.position - origin) > rangeSq) {
                continue;
            }
            if (world.flowField().distance(Grid::cellOf(enemy.position)) == bestDistance) {
                ++tiedCount;
            }
        }
    }
    return best;
}

// Bit-exact float comparison, spelled out rather than written as `a == b`.
//
// The build treats == on floats as an error, and rightly: almost every use of
// it is a tolerance bug. This is the exception the rule is guarding against
// finding -- the question here is whether two runs produced the identical
// value, not whether they produced a close one, so a tolerance would let a real
// divergence through. Comparing the bits says that outright.
bool sameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool storedInIdOrder(Span<const Enemy> enemies) {
    for (std::size_t i = 1; i < enemies.size(); ++i) {
        if (enemies[i].id < enemies[i - 1].id) {
            return false;
        }
    }
    return true;
}

}  // namespace

// The property that makes the whole storage refactor safe.
//
// Ties are not an exotic case: toGoal is a coarse per-tile BFS integer, so any
// two enemies on tiles equidistant from the goal tie, and a tower covering ~40
// tiles sees plenty of those. Before pooling, array position and id agreed --
// the vector was only ever appended to and erased in order -- so a tie-break
// that read array position would have looked correct indefinitely. Swap-and-pop
// destroys that agreement.
//
// So the test builds the tie itself and then permutes storage underneath it. If
// the rule reads position, the answer moves; if it reads identity, it cannot.
TEST(targeting_ties_break_on_handle_not_position) {
    Grid grid(config::kGridWidth, config::kGridHeight);
    const Cell goal{config::kGridWidth - 1, config::kGridHeight / 2};
    FlowField flow;
    flow.build(grid, goal);

    // Three enemies stacked in one tile: identical flow distance, so all three
    // tie and only the id can separate them. Ids are assigned so that the lowest
    // id is neither first nor last in the initial array -- otherwise "picks the
    // lowest id" and "picks the first" or "picks the last" would be
    // indistinguishable, and the test would pass for the wrong reason.
    const Cell towerCell{4, config::kGridHeight / 2};
    const Cell occupied{6, config::kGridHeight / 2};
    const Vec2 center = Grid::centerOf(occupied);

    std::vector<Enemy> enemies(3);
    enemies[0].id = 40;
    enemies[0].position = {center.x - 0.2f, center.y};
    enemies[1].id = 17;  // the winner, in the middle
    enemies[1].position = {center.x, center.y + 0.1f};
    enemies[2].id = 63;
    enemies[2].position = {center.x + 0.2f, center.y - 0.1f};

    for (const Enemy& e : enemies) {
        CHECK_EQ(flow.distance(Grid::cellOf(e.position)), flow.distance(occupied));
    }

    SpatialIndex<Enemy> index;

    // Every ordering of the same three enemies. A pool's dense array can be in
    // any of them after a few removals, and the rule has to be blind to which.
    const std::size_t orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                      {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (const auto& order : orders) {
        std::vector<Enemy> permuted{enemies[order[0]], enemies[order[1]], enemies[order[2]]};
        index.rebuild(grid.width(), grid.height(), Span<const Enemy>(permuted));

        const std::uint32_t slot = selectTarget<Enemy>(Span<const Enemy>(permuted), index, flow,
                                                       towerCell, config::kTowerRange);
        CHECK(slot != kNoTarget);
        CHECK_EQ(permuted[slot].id, std::uint32_t(17));
    }

    // Distance still outranks identity: an enemy closer to the goal wins even
    // with the highest id of the three. Without this the test would also pass
    // for a rule that just returned the lowest id in range.
    std::vector<Enemy> withLeader = enemies;
    Enemy leader;
    leader.id = 999;
    leader.position = Grid::centerOf({7, config::kGridHeight / 2});
    withLeader.push_back(leader);
    CHECK(flow.distance(Grid::cellOf(leader.position)) < flow.distance(occupied));

    index.rebuild(grid.width(), grid.height(), Span<const Enemy>(withLeader));
    const std::uint32_t slot = selectTarget<Enemy>(Span<const Enemy>(withLeader), index, flow,
                                                   towerCell, config::kTowerRange);
    CHECK(slot != kNoTarget);
    CHECK_EQ(withLeader[slot].id, std::uint32_t(999));
}

// The rule above, checked against a real game rather than a constructed one:
// through thousands of ticks of a defended run, with the dense array genuinely
// scrambled by swap-and-pop, the indexed lookup must agree with a brute-force
// scan on every tower on every tick.
//
// This is the half that would catch a mistake in the spatial index -- a box that
// is a tile too narrow, an entity dropped at a board edge -- which the
// constructed test above cannot see because it hands the index a layout it
// chose.
TEST(indexed_targeting_matches_bruteforce_in_a_real_run) {
    World world(config::kGridWidth, config::kGridHeight, 31337);

    const int lane = config::kGridHeight / 2;
    for (int x = 3; x <= 7; ++x) {
        world.placeTower({x, lane - 1});
        world.placeTower({x, lane + 1});
    }
    CHECK(!world.towers().empty());

    int ticksWithScrambledStorage = 0;
    int checks = 0;
    int previousRemoved = 0;

    for (int tick = 0; tick < 4000 && world.state() == GameState::Playing; ++tick) {
        world.step();

        // Towers aim during the step, before projectiles resolve and kill. If
        // anything died this tick the tower may be pointing at an enemy that is
        // no longer in the list, and recomputing the rule against the post-step
        // survivors would compare two different moments. Skip those ticks rather
        // than weaken the comparison; there are thousands of quiet ones, and the
        // assertions at the end make sure enough were actually checked.
        const int removed = world.enemiesKilled() + world.enemiesLeaked();
        const bool somethingDied = removed != previousRemoved;
        previousRemoved = removed;
        if (somethingDied || world.enemies().empty()) {
            continue;
        }

        if (!storedInIdOrder(world.enemies())) {
            ++ticksWithScrambledStorage;
        }

        for (const Tower& tower : world.towers()) {
            int tied = 0;
            const Enemy* expected = bruteForceTarget(world, tower, tied);
            if (expected == nullptr) {
                continue;
            }
            // aim is the observable consequence of the targeting choice: the
            // tower re-points at its target every tick, whether or not it can
            // fire.
            const Vec2 origin = Grid::centerOf(tower.cell);
            const Vec2 wanted = normalized(expected->position - origin);
            if (!sameBits(tower.aim.x, wanted.x) || !sameBits(tower.aim.y, wanted.y)) {
                testing::reportFailure(__FILE__, __LINE__,
                                       "tower at tick " + std::to_string(tick) +
                                           " is not aimed at the enemy the rule names");
                return;
            }
            ++checks;
        }
    }

    // A test that never reached the interesting state would pass while proving
    // nothing. These two stop that: the run must have happened, and storage must
    // actually have gone out of id order.
    CHECK(checks > 100);
    CHECK(ticksWithScrambledStorage > 0);
}

// known-wrong #6. The goal test used to ask whether the enemy was within
// kEnemyRadius of the goal centre, so an enemy that stepped over the centre
// registered no leak and cost no life. Every enemy that reaches the goal cell
// must be accounted for, exactly once.
TEST(enemies_that_reach_the_goal_are_always_counted) {
    World world;

    int previousLives = world.lives();
    int previousLeaked = world.enemiesLeaked();

    for (int tick = 0; tick < 3000 && world.state() == GameState::Playing; ++tick) {
        world.step();

        // Lives and leaks move together, one for one, and only downward.
        const int leakedNow = world.enemiesLeaked();
        const int livesNow = world.lives();
        CHECK_EQ(leakedNow - previousLeaked, previousLives - livesNow);
        previousLeaked = leakedNow;
        previousLives = livesNow;

        // No enemy may still be standing on the goal after a step: reaching it
        // is what removes it. A survivor here is an enemy that walked through.
        for (const Enemy& enemy : world.enemies()) {
            CHECK(!(Grid::cellOf(enemy.position) == world.goal()));
        }
    }

    CHECK(world.enemiesLeaked() > 0);  // an undefended run must leak
}

// known-wrong #4. The placement preview ran every frame from draw(), and every
// call copied the entire Grid and ran a fresh BFS with a fresh std::deque. The
// hover cell usually has not moved between frames, so almost all of that work
// was for an answer already known.
TEST(placement_preview_allocates_nothing) {
    World world;
    const Cell hovered{5, 3};

    // First call is allowed to allocate: it grows the reusable BFS buffers.
    (void)world.canPlaceAt(hovered);

    // Steady state: the same cell, the grid unchanged. This is the frame loop.
    const std::size_t before = g_allocations;
    bool result = false;
    for (int frame = 0; frame < 1000; ++frame) {
        result = world.canPlaceAt(hovered);
    }
    const std::size_t during = g_allocations - before;
    CHECK_EQ(during, std::size_t(0));
    CHECK(result);

    // Moving the mouse is a memo miss, but the buffers are already sized, so
    // even the miss must not allocate.
    const std::size_t beforeMoves = g_allocations;
    for (int frame = 0; frame < 200; ++frame) {
        (void)world.canPlaceAt({3 + (frame % 8), 2 + (frame % 5)});
    }
    CHECK_EQ(g_allocations - beforeMoves, std::size_t(0));

    // And the memo must not go stale. Placing a tower changes the grid, so the
    // cached answer for a cell it seals off has to be recomputed rather than
    // replayed. A memo that never invalidates would pass every assertion above.
    World fresh;
    const Cell probe{1, config::kGridHeight / 2 - 1};
    CHECK(fresh.canPlaceAt(probe));
    CHECK(fresh.placeTower(probe) == PlacementResult::Placed);
    CHECK(!fresh.canPlaceAt(probe));  // now occupied
}

// step() is on the 60 Hz path, so it must not allocate either -- a per-tick
// allocation is a per-tick chance to stall, and the scratch buffers exist
// precisely to avoid it.
TEST(steady_state_step_allocates_nothing) {
    World world;
    const int lane = config::kGridHeight / 2;
    for (int x = 3; x <= 6; ++x) {
        world.placeTower({x, lane - 1});
        world.placeTower({x, lane + 1});
    }

    // Warm up: let the pools and scratch buffers reach the size a busy wave
    // needs before measuring. Growth is a one-off cost, not a per-tick one.
    for (int tick = 0; tick < 1200; ++tick) {
        world.step();
    }

    const std::size_t before = g_allocations;
    for (int tick = 0; tick < 600 && world.state() == GameState::Playing; ++tick) {
        world.step();
    }
    CHECK_EQ(g_allocations - before, std::size_t(0));
}

// Reports timing, asserts none.
//
// CI timing is far too noisy to gate on -- a shared runner under load can be an
// order of magnitude slower than the same code on a quiet one -- so a
// wall-clock assertion here would be a flaky test that eventually gets deleted
// or, worse, retried until it passes. What it does assert is the thing that is
// actually deterministic: that the indexed query returns exactly what the
// O(P x E) scan it replaced would return, at a scale where the difference
// matters.
//
// Scope, stated plainly: this drives the real Pool, the real SpatialIndex and
// the real targeting predicate at 5,000 entities, but not World::step(), because
// nothing today can put 5,000 enemies into a World -- wave size is a function of
// the wave number and lives run out first. A stress test that steps the real
// world needs a way to author a starting state, which is Phase 3's content work.
TEST(stress_5000_enemies) {
    constexpr int kCount = 5000;
    constexpr int kWidth = 96;
    constexpr int kHeight = 64;

    Pool<Enemy, EnemyTag> enemies;
    enemies.reserve(kCount);
    Rng rng(0x5EED5EEDu);
    for (int i = 0; i < kCount; ++i) {
        Enemy enemy;
        enemy.position = {rng.range(0.0f, static_cast<float>(kWidth)),
                          rng.range(0.0f, static_cast<float>(kHeight))};
        enemy.health = 10.0f;
        enemy.maxHealth = 10.0f;
        enemy.speed = 1.0f;
        enemies.create(enemy);
    }

    // Churn, so the dense array is not in id order and the free list has been
    // exercised -- measuring a pristine pool would measure the easy case.
    for (int i = 0; i < kCount / 4; ++i) {
        enemies.destroy(enemies.handleAt(static_cast<std::size_t>(i)));
    }
    CHECK(!storedInIdOrder(enemies.items()));

    SpatialIndex<Enemy> index;

    const auto rebuildStart = std::chrono::steady_clock::now();
    constexpr int kRebuilds = 100;
    for (int i = 0; i < kRebuilds; ++i) {
        index.rebuild(kWidth, kHeight, enemies.items());
    }
    const auto rebuildEnd = std::chrono::steady_clock::now();

    // 200 towers querying once each is a heavy late-game board.
    constexpr int kTowers = 200;
    constexpr float kRange = config::kTowerRange;
    std::vector<Cell> towers;
    for (int i = 0; i < kTowers; ++i) {
        towers.push_back({rng.rangeInt(0, kWidth - 1), rng.rangeInt(0, kHeight - 1)});
    }

    std::size_t indexedHits = 0;
    const auto queryStart = std::chrono::steady_clock::now();
    for (const Cell cell : towers) {
        const Vec2 origin = Grid::centerOf(cell);
        const int reach = math::floorToInt(kRange) + 1;
        index.forEachInBox(
            {cell.x - reach, cell.y - reach}, {cell.x + reach, cell.y + reach},
            [&](std::uint32_t slot) {
                if (lengthSq(enemies.items()[slot].position - origin) <= kRange * kRange) {
                    ++indexedHits;
                }
            });
    }
    const auto queryEnd = std::chrono::steady_clock::now();

    // The same questions, answered the way the old code answered them.
    std::size_t bruteHits = 0;
    const auto bruteStart = std::chrono::steady_clock::now();
    for (const Cell cell : towers) {
        const Vec2 origin = Grid::centerOf(cell);
        for (const Enemy& enemy : enemies.items()) {
            if (lengthSq(enemy.position - origin) <= kRange * kRange) {
                ++bruteHits;
            }
        }
    }
    const auto bruteEnd = std::chrono::steady_clock::now();

    // The assertion that is worth making: same answers, at scale.
    CHECK_EQ(indexedHits, bruteHits);

    const auto us = [](auto a, auto b) {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
    };
    std::printf("    [stress] %d live enemies, %d towers\n", static_cast<int>(enemies.size()),
                kTowers);
    std::printf("    [stress] index rebuild:      %8.1f us/tick\n",
                us(rebuildStart, rebuildEnd) / kRebuilds);
    std::printf("    [stress] indexed queries:    %8.1f us/tick  (%zu hits)\n",
                us(queryStart, queryEnd), indexedHits);
    std::printf("    [stress] brute-force scan:   %8.1f us/tick  (the code this replaced)\n",
                us(bruteStart, bruteEnd));
}
