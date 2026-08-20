#pragma once

#include "game/Grid.h"

#include <vector>

namespace ktd {

// Breadth-first flow field over the grid, built backwards from the goal.
//
// A flow field rather than a per-enemy A* path: tower defense recomputes routes
// whenever a tower is placed, and every enemy shares the same destination. One
// BFS per placement is cheaper than N path searches, and enemies only ever need
// to ask "which way from the tile I'm standing on".
class FlowField {
public:
    void build(const Grid& grid, Cell goal);

    bool reachable(Cell c) const;

    // Steps from the goal, or -1 if the cell cannot reach it.
    int distance(Cell c) const;

    // The neighbouring cell one step closer to the goal. Returns c itself when
    // c is the goal or is unreachable.
    Cell next(Cell c) const;

    Cell goal() const { return goal_; }

private:
    int width_ = 0;
    int height_ = 0;
    Cell goal_{};
    std::vector<int> dist_;  // -1 = unreachable

    bool inBounds(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_; }
    int index(Cell c) const { return c.y * width_ + c.x; }
};

// True if a walkable route exists from `from` to `to`.
bool pathExists(const Grid& grid, Cell from, Cell to);

// Whether a tower may be placed at `cell`: the tile must be open, and the
// placement must not completely wall the spawn off from the goal. This is the
// classic maze-TD rule, and it is why placement needs the pathfinder.
bool canPlaceTower(const Grid& grid, Cell cell, Cell spawn, Cell goal);

}  // namespace ktd
