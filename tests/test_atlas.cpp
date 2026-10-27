#include "TestFramework.h"
#include "game/Rng.h"
#include "renderer/Atlas.h"
#include "renderer/AtlasBuilder.h"

#include <cstdint>
#include <string>
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

// --- AtlasBuilder: packing plus the pixels ---------------------------------

namespace {

// A solid sprite whose every texel encodes (sprite index, x, y), so a copy that
// lands at the wrong offset, row or stride is caught texel by texel.
std::vector<std::uint8_t> signedSprite(int index, int width, int height) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y * width + x) * 4u;
            rgba[at + 0] = static_cast<std::uint8_t>(index + 1);
            rgba[at + 1] = static_cast<std::uint8_t>(x);
            rgba[at + 2] = static_cast<std::uint8_t>(y);
            rgba[at + 3] = 0xFF;
        }
    }
    return rgba;
}

const std::uint8_t* atlasTexel(const BuiltAtlas& atlas, int x, int y) {
    return atlas.rgba.data() + static_cast<std::size_t>(y * atlas.width + x) * 4u;
}

}  // namespace

TEST(atlas_builder_copies_pixels_into_placements) {
    const int sizes[][2] = {{5, 3}, {2, 7}, {6, 6}, {1, 1}, {4, 2}};
    std::vector<std::vector<std::uint8_t>> pixels;
    std::vector<PixelView> views;
    for (int i = 0; i < 5; ++i) {
        pixels.push_back(signedSprite(i, sizes[i][0], sizes[i][1]));
    }
    for (int i = 0; i < 5; ++i) {
        views.push_back({sizes[i][0], sizes[i][1], pixels[static_cast<std::size_t>(i)].data()});
    }

    BuiltAtlas atlas;
    std::string error;
    CHECK(buildAtlas({views.data(), views.size()}, 16, 16, 1, atlas, error));
    CHECK_EQ(atlas.rgba.size(), std::size_t{16 * 16 * 4});
    CHECK_EQ(atlas.placements.size(), std::size_t{5});

    // Every texel is either exactly the one sprite placed over it, or
    // transparent black -- so padding gutters cannot have been written into.
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const std::uint8_t* t = atlasTexel(atlas, x, y);
            int owner = -1;
            for (int i = 0; i < 5; ++i) {
                const AtlasRect& r = atlas.placements[static_cast<std::size_t>(i)];
                if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
                    owner = i;
                }
            }
            if (owner < 0) {
                CHECK(t[0] == 0 && t[1] == 0 && t[2] == 0 && t[3] == 0);
                continue;
            }
            const AtlasRect& r = atlas.placements[static_cast<std::size_t>(owner)];
            CHECK_EQ(int{t[0]}, owner + 1);
            CHECK_EQ(int{t[1]}, x - r.x);
            CHECK_EQ(int{t[2]}, y - r.y);
            CHECK_EQ(int{t[3]}, 0xFF);
        }
    }
}

TEST(atlas_builder_uvs_address_texel_edges) {
    // UVs are the placement divided by the atlas size, nothing added. A
    // half-texel inset is what a bilinear atlas wants; with GL_NEAREST and a
    // padding gutter it would shave half a texel off every edge of the art.
    const std::vector<std::uint8_t> a = signedSprite(0, 8, 4);
    const std::vector<std::uint8_t> b = signedSprite(1, 3, 5);
    const PixelView views[] = {{8, 4, a.data()}, {3, 5, b.data()}};

    BuiltAtlas atlas;
    std::string error;
    CHECK(buildAtlas({views, 2}, 32, 16, 1, atlas, error));
    CHECK_EQ(atlas.uvs.size(), std::size_t{2});

    for (std::size_t i = 0; i < 2; ++i) {
        const AtlasRect& r = atlas.placements[i];
        const UvRect& uv = atlas.uvs[i];
        // Exact: these are small integers over powers of two.
        CHECK_NEAR(uv.u0, r.x / 32.0, 0.0);
        CHECK_NEAR(uv.v0, r.y / 16.0, 0.0);
        CHECK_NEAR(uv.u1, (r.x + r.width) / 32.0, 0.0);
        CHECK_NEAR(uv.v1, (r.y + r.height) / 16.0, 0.0);
    }
}

TEST(atlas_builder_fails_loudly_when_full) {
    const std::vector<std::uint8_t> big = signedSprite(0, 6, 6);
    const std::vector<std::uint8_t> small = signedSprite(1, 2, 2);
    const PixelView views[] = {{2, 2, small.data()}, {6, 6, big.data()}, {2, 2, small.data()}};

    BuiltAtlas atlas;
    atlas.width = 99;  // sentinel: a failure must not touch `out`
    std::string error;
    CHECK(!buildAtlas({views, 3}, 8, 8, 1, atlas, error));
    // Names the sprite that did not fit, so the fix is obvious from the log.
    CHECK(error.find("sprite 1") != std::string::npos);
    CHECK_EQ(atlas.width, 99);
    CHECK(atlas.rgba.empty());
}
