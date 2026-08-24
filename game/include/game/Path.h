#pragma once

#include "game/Grid.h"

#include <cstdint>
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
    //
    // Answered from a direction cached during build(), not by re-scanning the
    // four neighbours. Every enemy calls this every tick, and the scan it
    // replaces re-derived, per enemy per tick, something the BFS already knew
    // when it built the field.
    Cell next(Cell c) const;

    Cell goal() const { return goal_; }

private:
    int width_ = 0;
    int height_ = 0;
    Cell goal_{};
    std::vector<int> dist_;  // -1 = unreachable

    // Index into kNeighbours of the step toward the goal, or kNoDirection.
    // Stored as a byte: one per tile of the board, and the board is small.
    std::vector<std::uint8_t> dir_;

    // Reused across builds so a tower placement does not allocate. build() is
    // not on the per-tick path, but it is on the per-click path, and the buffer
    // costs one vector to keep.
    std::vector<Cell> queue_;

    bool inBounds(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < width_ && c.y < height_; }
    // size_t for the same reason as Grid::index -- see the note there.
    std::size_t index(Cell c) const { return static_cast<std::size_t>(c.y * width_ + c.x); }
};

// Repeated "would this placement seal the route?" queries with no allocation.
//
// The old canPlaceTower copied the entire Grid, set one tile on the copy, and
// ran a fresh BFS with a fresh std::deque -- and the placement preview called it
// once per frame from draw(), for a hover cell that usually had not moved
// (known-wrong #4). This holds its BFS buffers and answers the same question by
// treating one cell as blocked, so it neither copies nor mutates the grid.
class PlacementQuery {
public:
    // True if `from` can still reach `to` when `blocked` is treated as a wall.
    bool routeSurvives(const Grid& grid, Cell blocked, Cell from, Cell to);

private:
    std::vector<std::uint8_t> seen_;
    std::vector<Cell> queue_;
};

// True if a walkable route exists from `from` to `to`.
bool pathExists(const Grid& grid, Cell from, Cell to);

// Whether a tower may be placed at `cell`: the tile must be open, and the
// placement must not completely wall the spawn off from the goal. This is the
// classic maze-TD rule, and it is why placement needs the pathfinder.
//
// Allocates. Fine for tests and for the one-shot check inside placeTower();
// anything asking this every frame wants World::canPlaceAt, which memoizes.
bool canPlaceTower(const Grid& grid, Cell cell, Cell spawn, Cell goal);

}  // namespace ktd
