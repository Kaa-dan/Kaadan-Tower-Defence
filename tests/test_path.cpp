#include "TestFramework.h"

#include "game/Path.h"

using namespace ktd;

TEST(flow_field_measures_manhattan_distance_on_open_ground) {
    Grid grid(6, 6);
    FlowField field;
    field.build(grid, {5, 5});

    CHECK_EQ(field.distance({5, 5}), 0);
    CHECK_EQ(field.distance({0, 5}), 5);
    // No diagonal movement, so the corner is the sum of both axes.
    CHECK_EQ(field.distance({0, 0}), 10);
}

TEST(flow_field_steps_strictly_closer_to_the_goal) {
    Grid grid(8, 8);
    grid.set({4, 0}, Tile::Wall);
    grid.set({4, 1}, Tile::Wall);
    grid.set({4, 2}, Tile::Wall);
    grid.set({4, 3}, Tile::Wall);

    FlowField field;
    field.build(grid, {7, 0});

    // Walk the field from a start cell and confirm it terminates at the goal
    // rather than looping -- the property enemies actually depend on.
    Cell cursor{0, 0};
    int steps = 0;
    while (cursor != Cell{7, 0} && steps < 200) {
        const Cell next = field.next(cursor);
        CHECK(next != cursor);
        CHECK(field.distance(next) < field.distance(cursor));
        cursor = next;
        ++steps;
    }
    CHECK(cursor == Cell{7, 0});
}

TEST(walled_off_cells_are_unreachable) {
    Grid grid(5, 5);
    // Box the corner in completely.
    grid.set({1, 0}, Tile::Wall);
    grid.set({1, 1}, Tile::Wall);
    grid.set({0, 1}, Tile::Wall);

    FlowField field;
    field.build(grid, {4, 4});

    CHECK(!field.reachable({0, 0}));
    CHECK_EQ(field.distance({0, 0}), -1);
    // next() on an unreachable cell must not wander.
    CHECK(field.next({0, 0}) == Cell{0, 0});
    CHECK(field.reachable({4, 0}));
}

TEST(unreachable_goal_yields_an_empty_field) {
    Grid grid(4, 4);
    grid.set({2, 2}, Tile::Wall);

    FlowField field;
    field.build(grid, {2, 2});  // goal sits inside a wall

    CHECK(!field.reachable({0, 0}));
}

TEST(placement_is_rejected_when_it_would_seal_the_route) {
    // A single-tile corridor: the only way through is (1,1).
    Grid grid(3, 3);
    grid.set({1, 0}, Tile::Wall);
    grid.set({1, 2}, Tile::Wall);

    const Cell spawn{0, 1};
    const Cell goal{2, 1};

    CHECK(pathExists(grid, spawn, goal));
    CHECK(!canPlaceTower(grid, {1, 1}, spawn, goal));  // would seal it
    CHECK(canPlaceTower(grid, {0, 0}, spawn, goal));   // harmless corner
}

TEST(placement_is_rejected_on_spawn_goal_and_occupied_tiles) {
    Grid grid(5, 5);
    grid.set({3, 3}, Tile::Wall);
    const Cell spawn{0, 2};
    const Cell goal{4, 2};

    CHECK(!canPlaceTower(grid, spawn, spawn, goal));
    CHECK(!canPlaceTower(grid, goal, spawn, goal));
    CHECK(!canPlaceTower(grid, {3, 3}, spawn, goal));
    CHECK(!canPlaceTower(grid, {9, 9}, spawn, goal));
}
