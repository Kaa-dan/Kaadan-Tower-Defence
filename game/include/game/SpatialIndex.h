#pragma once

#include "game/Grid.h"
#include "game/Span.h"

#include <cstdint>
#include <vector>

namespace ktd {

// A uniform tile-aligned bucket grid, rebuilt from scratch every tick.
//
// Not a quadtree. Entities here are point-sized, densely packed into a board
// that is a few hundred tiles, and every one of them moves every tick -- which
// is the worst case for a tree and the best case for a bucket array. Rebuilding
// costs one counting sort: O(N), no comparisons, no allocation after the first
// tick, and stable by construction, so it is deterministic without anyone
// having to be careful.
//
// Rebuilding beats incremental updates for the same reason. An index that is
// mutated as entities move is an index that can be stale, and a stale spatial
// index shows up as a tower that inexplicably ignores an enemy standing in
// front of it. Rebuilding cannot be stale.
//
// T must expose a `Vec2 position`. Buckets hold indices into the caller's array
// rather than copies, so the index stays valid for exactly as long as the array
// it was built from -- which is one tick.
template <typename T>
class SpatialIndex {
public:
    void rebuild(int width, int height, Span<const T> items) {
        width_ = width;
        height_ = height;
        const std::size_t cells = static_cast<std::size_t>(width_ * height_);

        // assign() rather than a fresh vector: after the first tick the
        // capacity is already there, so none of this allocates.
        offsets_.assign(cells + 1, 0);
        entries_.resize(items.size());

        // Counting sort, pass 1: how many land in each bucket. Counts are
        // written one slot to the right so the prefix sum in pass 2 turns them
        // into start offsets in place.
        for (const T& item : items) {
            ++offsets_[bucket(item.position) + 1];
        }
        for (std::size_t i = 1; i <= cells; ++i) {
            offsets_[i] += offsets_[i - 1];
        }

        // Pass 2: place. cursor_ walks each bucket's run as it fills.
        cursor_.assign(offsets_.begin(), offsets_.end() - 1);
        for (std::size_t i = 0; i < items.size(); ++i) {
            entries_[cursor_[bucket(items[i].position)]++] = static_cast<std::uint32_t>(i);
        }
    }

    // Calls fn(index) for every entry in the inclusive cell box, clamped to the
    // board. Callers still apply the exact distance test: this narrows the
    // candidate set, it does not decide the answer. That split is what keeps it
    // equivalent to a brute-force scan rather than an approximation of one.
    template <typename Fn>
    void forEachInBox(Cell low, Cell high, Fn&& fn) const {
        // Both ends of the box are clamped with the same rule bucket() uses, and
        // that symmetry is what makes the index equivalent to a brute-force scan
        // rather than nearly equivalent.
        //
        // Clamping only the near ends looks sufficient and is not: a box that
        // lies entirely off the board collapses to an empty range, so an entity
        // that bucket() clamped onto the edge becomes unreachable and the index
        // silently misses it. Clamping is monotonic, so applying it to the box
        // and to the entity's cell keeps "the entity's bucket lies within the
        // box" true on both sides of the clamp.
        if (width_ <= 0 || height_ <= 0) {
            return;
        }
        const int x0 = clampAxis(low.x, width_);
        const int y0 = clampAxis(low.y, height_);
        const int x1 = clampAxis(high.x, width_);
        const int y1 = clampAxis(high.y, height_);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::size_t cell = static_cast<std::size_t>(y * width_ + x);
                for (std::size_t i = offsets_[cell]; i < offsets_[cell + 1]; ++i) {
                    fn(entries_[i]);
                }
            }
        }
    }

private:
    // Positions are clamped into the board rather than dropped.
    //
    // Nothing in the simulation puts an entity outside the grid today, and the
    // movement code makes it hard to. But dropping an out-of-bounds entity would
    // make the index disagree with a brute-force scan, and this index's entire
    // contract is that it does not. Clamping keeps them findable: a query whose
    // box reaches the edge sees them and rejects them on the exact distance
    // test, and a query that does not reach the edge would have rejected them
    // anyway.
    static int clampAxis(int v, int extent) { return v < 0 ? 0 : (v >= extent ? extent - 1 : v); }

    std::size_t bucket(Vec2 p) const {
        const Cell c = Grid::cellOf(p);
        return static_cast<std::size_t>(clampAxis(c.y, height_) * width_ + clampAxis(c.x, width_));
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<std::size_t> offsets_;  // cells + 1 entries; bucket i is [i, i+1)
    std::vector<std::size_t> cursor_;
    std::vector<std::uint32_t> entries_;
};

}  // namespace ktd
