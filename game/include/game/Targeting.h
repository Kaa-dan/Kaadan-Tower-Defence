#pragma once

#include "game/Grid.h"
#include "game/Path.h"
#include "game/Span.h"
#include "game/SpatialIndex.h"
#include "game/Types.h"

#include <cstdint>
#include <limits>

namespace ktd {

inline constexpr std::uint32_t kNoTarget = 0xFFFFFFFFu;

// "Furthest along the path" targeting: of everything in range, shoot the enemy
// closest to the goal. The standard TD default, because it prioritises the leaks
// that actually cost the player a life.
//
// A free function rather than a World method, and templated rather than tied to
// Enemy, for one reason: the tie-break below is the property the whole storage
// refactor rests on, and it cannot be exercised through World. Ties need two
// enemies at the same flow distance inside one tower's range, and the current
// level cannot produce that -- a single lane, one spawn every 45 ticks, one
// speed per wave, so enemies are strung out more than a tile apart and never
// share a BFS distance. Left as a private method, the rule would have been
// covered only by a test that could not reach it.
//
// T needs `Vec2 position` and `std::uint32_t id`.
template <typename T>
std::uint32_t selectTarget(Span<const T> enemies, const SpatialIndex<T>& index,
                           const FlowField& flow, Cell towerCell, float range) {
    const Vec2 origin = Grid::centerOf(towerCell);
    const float rangeSq = range * range;

    std::uint32_t best = kNoTarget;
    std::uint32_t bestId = 0;
    int bestDistance = std::numeric_limits<int>::max();

    // Only the tiles the range can reach, rather than every enemy on the board.
    // The box is one tile wider than the range on each side, which provably
    // contains every cell a point within `range` of the tower's centre can
    // occupy; the exact distance test below still decides. That split is what
    // makes this identical to the brute-force scan it replaced instead of an
    // approximation of it.
    const int reach = math::floorToInt(range) + 1;
    const Cell low{towerCell.x - reach, towerCell.y - reach};
    const Cell high{towerCell.x + reach, towerCell.y + reach};

    index.forEachInBox(low, high, [&](std::uint32_t slot) {
        const T& enemy = enemies[slot];
        if (lengthSq(enemy.position - origin) > rangeSq) {
            return;
        }
        const int toGoal = flow.distance(Grid::cellOf(enemy.position));
        if (toGoal < 0) {
            return;
        }
        // Ties are broken on the entity's stable id, never on where it sits in
        // storage. Before pooling, the two agreed -- the vector was only ever
        // appended to and erased in order -- so a position-based tie-break would
        // have looked correct indefinitely. Swap-and-pop makes array position a
        // function of destruction history, and a rule that read it would change
        // its answer the first time an unrelated enemy died, invalidating every
        // replay recorded before that moment with nothing in the code to explain
        // why. See ADR 0004 and ADR 0009.
        if (best == kNoTarget || toGoal < bestDistance ||
            (toGoal == bestDistance && enemy.id < bestId)) {
            bestDistance = toGoal;
            bestId = enemy.id;
            best = slot;
        }
    });

    return best;
}

}  // namespace ktd
