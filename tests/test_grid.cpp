#include "TestFramework.h"

#include "game/Grid.h"

using namespace ktd;

TEST(grid_starts_fully_open) {
    Grid grid(8, 5);
    CHECK_EQ(grid.width(), 8);
    CHECK_EQ(grid.height(), 5);

    for (int y = 0; y < grid.height(); ++y) {
        for (int x = 0; x < grid.width(); ++x) {
            CHECK(grid.walkable({x, y}));
        }
    }
}

TEST(grid_bounds_are_exclusive_at_the_far_edge) {
    Grid grid(4, 3);
    CHECK(grid.inBounds({0, 0}));
    CHECK(grid.inBounds({3, 2}));
    CHECK(!grid.inBounds({4, 2}));
    CHECK(!grid.inBounds({3, 3}));
    CHECK(!grid.inBounds({-1, 0}));
}

TEST(walls_and_towers_block_walking) {
    Grid grid(4, 4);
    grid.set({1, 1}, Tile::Wall);
    grid.set({2, 2}, Tile::Tower);

    CHECK(!grid.walkable({1, 1}));
    CHECK(!grid.walkable({2, 2}));
    CHECK(grid.walkable({0, 0}));
}

TEST(out_of_bounds_is_never_walkable) {
    Grid grid(4, 4);
    CHECK(!grid.walkable({-1, 2}));
    CHECK(!grid.walkable({4, 2}));
}

TEST(cell_and_world_coordinates_round_trip) {
    const Cell cell{3, 7};
    const Vec2 center = Grid::centerOf(cell);
    CHECK_NEAR(center.x, 3.5f, 1e-5);
    CHECK_NEAR(center.y, 7.5f, 1e-5);
    CHECK(Grid::cellOf(center) == cell);

    // Points anywhere inside the tile map back to the same cell.
    CHECK(Grid::cellOf({3.01f, 7.99f}) == cell);
}
