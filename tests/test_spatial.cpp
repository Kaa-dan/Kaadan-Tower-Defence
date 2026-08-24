#include "TestFramework.h"

#include "game/Rng.h"
#include "game/SpatialIndex.h"
#include "game/World.h"

#include <vector>

using namespace ktd;

namespace {

struct Point {
    std::uint32_t id = 0;
    Vec2 position{};
};

}  // namespace

// The index's entire contract: it narrows the candidate set and changes nothing
// else. An index that returns a *nearly* correct set is worse than no index --
// it makes towers ignore an enemy standing in front of them, intermittently, in
// a way no unit test that checks a single layout would catch.
//
// So this compares against the brute-force scan the index replaced, over
// randomized layouts, including the awkward cases: points on tile boundaries,
// points outside the board, and query boxes hanging off every edge.
TEST(spatial_index_matches_bruteforce) {
    constexpr int kWidth = 24;
    constexpr int kHeight = 16;

    Rng rng(0xC0FFEEu);
    SpatialIndex<Point> index;

    for (int trial = 0; trial < 200; ++trial) {
        const int count = rng.rangeInt(0, 120);
        std::vector<Point> points;
        points.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            Point p;
            p.id = static_cast<std::uint32_t>(i);
            // Deliberately overshoots the board on both axes, so the clamping
            // path is exercised rather than assumed unreachable.
            p.position = {rng.range(-2.0f, static_cast<float>(kWidth) + 2.0f),
                          rng.range(-2.0f, static_cast<float>(kHeight) + 2.0f)};
            points.push_back(p);
        }

        index.rebuild(kWidth, kHeight, Span<const Point>(points));

        for (int query = 0; query < 8; ++query) {
            const Cell center{rng.rangeInt(-2, kWidth + 1), rng.rangeInt(-2, kHeight + 1)};
            const float radius = rng.range(0.5f, 5.0f);
            const Vec2 origin = Grid::centerOf(center);

            const int reach = math::floorToInt(radius) + 1;
            const Cell low{center.x - reach, center.y - reach};
            const Cell high{center.x + reach, center.y + reach};

            // What the index finds, after the caller's exact distance test --
            // the same two-stage filter World::findTarget uses.
            std::vector<std::uint32_t> found;
            index.forEachInBox(low, high, [&](std::uint32_t slot) {
                if (lengthSq(points[slot].position - origin) <= radius * radius) {
                    found.push_back(points[slot].id);
                }
            });

            // What an O(N) scan would find.
            std::vector<std::uint32_t> expected;
            for (const Point& p : points) {
                if (lengthSq(p.position - origin) <= radius * radius) {
                    expected.push_back(p.id);
                }
            }

            // Sets, not sequences: the index is free to visit in bucket order.
            // What it may not do is miss one or invent one.
            if (found.size() != expected.size()) {
                testing::reportFailure(__FILE__, __LINE__,
                                       "trial " + std::to_string(trial) + ": index found " +
                                           std::to_string(found.size()) + ", brute force found " +
                                           std::to_string(expected.size()));
                continue;
            }
            for (const std::uint32_t id : expected) {
                bool present = false;
                for (const std::uint32_t candidate : found) {
                    if (candidate == id) {
                        present = true;
                    }
                }
                CHECK(present);
            }
        }
    }
}

// Rebuilding must be a pure function of the input. If it were not, the index
// would be a hidden source of per-run variation feeding straight into targeting.
TEST(spatial_index_rebuild_is_deterministic) {
    Rng rng(99u);
    std::vector<Point> points;
    for (int i = 0; i < 200; ++i) {
        points.push_back(
            {static_cast<std::uint32_t>(i), {rng.range(0.0f, 24.0f), rng.range(0.0f, 16.0f)}});
    }

    SpatialIndex<Point> a;
    SpatialIndex<Point> b;
    a.rebuild(24, 16, Span<const Point>(points));
    b.rebuild(24, 16, Span<const Point>(points));
    // Rebuilt twice on the same instance too: the buffers are reused, so a
    // stale entry left over from the previous build would show up here.
    a.rebuild(24, 16, Span<const Point>(points));

    std::vector<std::uint32_t> fromA;
    std::vector<std::uint32_t> fromB;
    a.forEachInBox({0, 0}, {23, 15}, [&](std::uint32_t s) { fromA.push_back(s); });
    b.forEachInBox({0, 0}, {23, 15}, [&](std::uint32_t s) { fromB.push_back(s); });

    CHECK_EQ(fromA.size(), points.size());
    CHECK_EQ(fromA.size(), fromB.size());
    for (std::size_t i = 0; i < fromA.size(); ++i) {
        CHECK_EQ(fromA[i], fromB[i]);
    }
}

// An empty board must produce an empty index rather than a stale one, and an
// index built for a board must not be walked with the previous board's extents.
TEST(spatial_index_handles_empty_input) {
    SpatialIndex<Point> index;
    std::vector<Point> points{{1, {3.5f, 3.5f}}};
    index.rebuild(24, 16, Span<const Point>(points));

    points.clear();
    index.rebuild(24, 16, Span<const Point>(points));

    int visits = 0;
    index.forEachInBox({-5, -5}, {100, 100}, [&](std::uint32_t) { ++visits; });
    CHECK_EQ(visits, 0);
}
