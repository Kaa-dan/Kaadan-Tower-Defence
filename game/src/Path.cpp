#include "game/Path.h"

namespace ktd {
namespace {

// 4-directional movement only. Allowing diagonals would let enemies slip
// through the corner gap between two diagonally-placed towers, which players
// reasonably read as a bug.
constexpr Cell kNeighbours[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

constexpr std::uint8_t kNoDirection = 0xFF;

}  // namespace

void FlowField::build(const Grid& grid, Cell goal) {
    width_ = grid.width();
    height_ = grid.height();
    goal_ = goal;
    const std::size_t cells = static_cast<std::size_t>(width_ * height_);
    dist_.assign(cells, -1);
    dir_.assign(cells, kNoDirection);

    if (!grid.walkable(goal)) {
        return;
    }

    // A vector plus a read cursor rather than a std::deque. The queue is only
    // ever pushed at the back and popped at the front, it is bounded by the
    // board, and the buffer is a member -- so this is a flat array walk with no
    // per-build allocation, where the deque paid for a chunk map to support a
    // front-insert that never happens.
    queue_.clear();
    dist_[index(goal)] = 0;
    queue_.push_back(goal);

    for (std::size_t head = 0; head < queue_.size(); ++head) {
        const Cell current = queue_[head];
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
            queue_.push_back(neighbour);
        }
    }

    // The direction pass, deliberately separate from the BFS.
    //
    // The obvious optimisation is to record the step the BFS took as it
    // discovers each cell -- the search already knows it. That is a different
    // answer. The BFS parent is whichever neighbour happened to reach the cell
    // first in queue order; the routing this replaces returned the FIRST
    // neighbour in kNeighbours order that was one step closer. Both are one step
    // closer to the goal, so both are correct routes and both are
    // deterministic -- and they are not the same cell, so silently swapping one
    // for the other would reroute enemies around the other side of an obstacle.
    //
    // On the default level the two happen to agree, so the golden hash did not
    // notice the difference. That is luck, not evidence, and it is exactly the
    // kind of change a level with a symmetric detour would surface later as an
    // unexplained replay desync. Deriving the direction from the finished
    // distance field reproduces the old routing by construction.
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Cell c{x, y};
            const int current = dist_[index(c)];
            if (current <= 0) {
                continue;  // the goal, or unreachable: no step to take
            }
            for (std::size_t d = 0; d < 4; ++d) {
                const Cell neighbour{c.x + kNeighbours[d].x, c.y + kNeighbours[d].y};
                if (!inBounds(neighbour)) {
                    continue;
                }
                const int candidate = dist_[index(neighbour)];
                if (candidate >= 0 && candidate < current) {
                    dir_[index(c)] = static_cast<std::uint8_t>(d);
                    break;
                }
            }
        }
    }
}

bool FlowField::reachable(Cell c) const { return inBounds(c) && dist_[index(c)] >= 0; }

int FlowField::distance(Cell c) const { return inBounds(c) ? dist_[index(c)] : -1; }

Cell FlowField::next(Cell c) const {
    if (!inBounds(c)) {
        return c;
    }
    const std::uint8_t d = dir_[index(c)];
    if (d == kNoDirection) {
        return c;  // the goal itself, or unreachable
    }
    return Cell{c.x + kNeighbours[d].x, c.y + kNeighbours[d].y};
}

bool PlacementQuery::routeSurvives(const Grid& grid, Cell blocked, Cell from, Cell to) {
    const int width = grid.width();
    const int height = grid.height();
    if (!grid.inBounds(from) || !grid.inBounds(to)) {
        return false;
    }

    seen_.assign(static_cast<std::size_t>(width * height), 0);
    queue_.clear();

    const auto passable = [&](Cell c) { return grid.walkable(c) && c != blocked; };
    const auto mark = [&](Cell c) -> std::uint8_t& {
        return seen_[static_cast<std::size_t>(c.y * width + c.x)];
    };

    if (!passable(to)) {
        return false;
    }
    mark(to) = 1;
    queue_.push_back(to);

    for (std::size_t head = 0; head < queue_.size(); ++head) {
        const Cell current = queue_[head];
        if (current == from) {
            return true;  // early out: this is a reachability question, not a distance one
        }
        for (const Cell offset : kNeighbours) {
            const Cell neighbour{current.x + offset.x, current.y + offset.y};
            if (!passable(neighbour) || mark(neighbour) != 0) {
                continue;
            }
            mark(neighbour) = 1;
            queue_.push_back(neighbour);
        }
    }
    return false;
}

bool pathExists(const Grid& grid, Cell from, Cell to) {
    PlacementQuery query;
    return query.routeSurvives(grid, Cell{-1, -1}, from, to);
}

bool canPlaceTower(const Grid& grid, Cell cell, Cell spawn, Cell goal) {
    if (!grid.inBounds(cell) || grid.at(cell) != Tile::Open) {
        return false;
    }
    if (cell == spawn || cell == goal) {
        return false;
    }

    PlacementQuery query;
    return query.routeSurvives(grid, cell, spawn, goal);
}

}  // namespace ktd
