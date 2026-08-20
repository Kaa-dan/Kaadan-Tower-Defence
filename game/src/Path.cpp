#include "game/Path.h"

#include <deque>

namespace ktd {
namespace {

// 4-directional movement only. Allowing diagonals would let enemies slip
// through the corner gap between two diagonally-placed towers, which players
// reasonably read as a bug.
constexpr Cell kNeighbours[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

}  // namespace

void FlowField::build(const Grid& grid, Cell goal) {
    width_ = grid.width();
    height_ = grid.height();
    goal_ = goal;
    dist_.assign(static_cast<std::size_t>(width_ * height_), -1);

    if (!grid.walkable(goal)) {
        return;
    }

    std::deque<Cell> queue;
    dist_[index(goal)] = 0;
    queue.push_back(goal);

    while (!queue.empty()) {
        const Cell current = queue.front();
        queue.pop_front();
        const int nextDist = dist_[index(current)] + 1;

        for (const Cell offset : kNeighbours) {
            const Cell neighbour{current.x + offset.x, current.y + offset.y};
            if (!grid.walkable(neighbour)) {
                continue;
            }
            if (dist_[index(neighbour)] != -1) {
                continue;
            }
            dist_[index(neighbour)] = nextDist;
            queue.push_back(neighbour);
        }
    }
}

bool FlowField::reachable(Cell c) const { return inBounds(c) && dist_[index(c)] >= 0; }

int FlowField::distance(Cell c) const { return inBounds(c) ? dist_[index(c)] : -1; }

Cell FlowField::next(Cell c) const {
    if (!reachable(c)) {
        return c;
    }

    const int currentDist = dist_[index(c)];
    if (currentDist == 0) {
        return c;  // already at the goal
    }

    for (const Cell offset : kNeighbours) {
        const Cell neighbour{c.x + offset.x, c.y + offset.y};
        if (!inBounds(neighbour)) {
            continue;
        }
        const int d = dist_[index(neighbour)];
        if (d >= 0 && d < currentDist) {
            return neighbour;
        }
    }
    return c;
}

bool pathExists(const Grid& grid, Cell from, Cell to) {
    FlowField field;
    field.build(grid, to);
    return field.reachable(from);
}

bool canPlaceTower(const Grid& grid, Cell cell, Cell spawn, Cell goal) {
    if (!grid.inBounds(cell) || grid.at(cell) != Tile::Open) {
        return false;
    }
    if (cell == spawn || cell == goal) {
        return false;
    }

    // Try the placement on a copy and see whether the route survives.
    Grid trial = grid;
    trial.set(cell, Tile::Tower);
    return pathExists(trial, spawn, goal);
}

}  // namespace ktd
