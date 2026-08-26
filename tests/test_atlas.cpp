#include "TestFramework.h"
#include "game/Rng.h"
#include "renderer/Atlas.h"

#include <vector>

using namespace ktd;
using namespace ktd::render;

namespace {

bool overlaps(const AtlasRect& a, const AtlasRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

// Smallest axis-aligned gap between two disjoint rects. Zero when they touch.
int gapBetween(const AtlasRect& a, const AtlasRect& b) {
    const int dx = (b.x >= a.x + a.width)   ? b.x - (a.x + a.width)
                   : (a.x >= b.x + b.width) ? a.x - (b.x + b.width)
                                            : -1;
    const int dy = (b.y >= a.y + a.height)   ? b.y - (a.y + a.height)
                   : (a.y >= b.y + b.height) ? a.y - (b.y + b.height)
                                             : -1;
    // Separated on at least one axis; the gap is the larger separation, since
    // touching on one axis while cleared on the other is still cleared.
    if (dx < 0) return dy;
    if (dy < 0) return dx;
    return dx > dy ? dx : dy;
}

}  // namespace

TEST(atlas_packer_places_without_overlap) {
    // Randomized rather than hand-picked: the failure mode of a skyline packer
    // is a specific sequence of heights that leaves a segment stale, and no
    // fixed sprite list is going to stumble onto it.
    Rng rng(0xA71A5u);

    for (int trial = 0; trial < 40; ++trial) {
        AtlasPacker packer(512, 512, 1);
        std::vector<AtlasRect> placed;

        for (int i = 0; i < 120; ++i) {
            const int w = rng.rangeInt(4, 63);
            const int h = rng.rangeInt(4, 63);
            AtlasRect rect{};
            if (!packer.place(w, h, rect)) {
                continue;  // full is legal; overlapping is not
            }

            CHECK_EQ(rect.width, w);
            CHECK_EQ(rect.height, h);
            CHECK(rect.x >= 0 && rect.y >= 0);
            CHECK(rect.x + rect.width <= 512);
            CHECK(rect.y + rect.height <= 512);

            for (const AtlasRect& other : placed) {
                CHECK(!overlaps(rect, other));
            }
            placed.push_back(rect);
        }
    }
}

TEST(atlas_reserves_padding_between_sprites) {
    // 1px of gutter, or a sampler at non-integer zoom reads across the seam and
    // the neighbouring sprite bleeds into the edge of this one.
    constexpr int kPadding = 1;
    Rng rng(0x5EED1u);
    AtlasPacker packer(256, 256, kPadding);

    std::vector<AtlasRect> placed;
    for (int i = 0; i < 60; ++i) {
        const int w = rng.rangeInt(8, 31);
        const int h = rng.rangeInt(8, 31);
        AtlasRect rect{};
        if (packer.place(w, h, rect)) {
            placed.push_back(rect);
        }
    }
    CHECK(placed.size() > 20);  // otherwise this proves nothing

    for (std::size_t i = 0; i < placed.size(); ++i) {
        for (std::size_t j = i + 1; j < placed.size(); ++j) {
            CHECK(gapBetween(placed[i], placed[j]) >= kPadding);
        }
    }
}

TEST(atlas_packer_reports_failure_when_full) {
    // A packer that silently drops what it cannot fit renders a blank sprite
    // and no error, which reads as an art problem rather than a capacity one.
    AtlasPacker packer(64, 64, 1);

    AtlasRect rect{};
    CHECK(packer.place(60, 60, rect));

    AtlasRect second{};
    CHECK(!packer.place(60, 60, second));
    CHECK(second == AtlasRect{});  // untouched on failure

    // Oversized and degenerate inputs fail rather than assert or wrap.
    CHECK(!packer.place(1000, 8, rect));
    CHECK(!packer.place(0, 8, rect));
    CHECK(!packer.place(8, -3, rect));
}

TEST(atlas_packing_is_reproducible) {
    // Same sprite list, same placements -- so the UVs baked into a build do not
    // depend on which run produced them.
    const int sizes[][2] = {{31, 17}, {8, 8}, {64, 12}, {5, 40}, {22, 22}, {13, 61}, {40, 9}};

    std::vector<AtlasRect> first;
    std::vector<AtlasRect> second;
    for (std::vector<AtlasRect>* out : {&first, &second}) {
        AtlasPacker packer(128, 128, 1);
        for (const auto& size : sizes) {
            AtlasRect rect{};
            if (packer.place(size[0], size[1], rect)) {
                out->push_back(rect);
            }
        }
    }

    CHECK_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i] == second[i]);
    }
}
