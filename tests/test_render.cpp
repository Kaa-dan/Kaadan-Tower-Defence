#include "TestFramework.h"
#include "renderer/Projection.h"
#include "renderer/Vertex.h"

#include <array>
#include <cmath>
#include <limits>

using namespace ktd;
using namespace ktd::render;

TEST(color_pack_clamps_out_of_range_channels) {
    // The in-range cases first, so a clamp that swallowed everything would not
    // pass by accident.
    CHECK_EQ(packRgba({0.0f, 0.0f, 0.0f, 0.0f}), 0x00000000u);
    CHECK_EQ(packRgba({1.0f, 1.0f, 1.0f, 1.0f}), 0xFFFFFFFFu);

    // r, g, b, a ascending in memory: red alone is the low byte.
    CHECK_EQ(packRgba({1.0f, 0.0f, 0.0f, 0.0f}), 0x000000FFu);
    CHECK_EQ(packRgba({0.0f, 0.0f, 0.0f, 1.0f}), 0xFF000000u);

    // Out of range is undefined behaviour if it reaches the conversion, and
    // these are the values that actually show up: a tint scaled past one by a
    // damage flash, a negative from a subtractive blend, and the infinity a
    // health fraction produces when maxHealth is zero.
    CHECK_EQ(packRgba({1.4f, -0.2f, 0.0f, 1.0f}), 0xFF0000FFu);

    const float infinity = std::numeric_limits<float>::infinity();
    CHECK_EQ(packRgba({infinity, -infinity, 0.0f, 1.0f}), 0xFF0000FFu);

    // NaN compares false against both bounds; the clamp is ordered so it lands
    // on 0 rather than on whatever the conversion would have produced.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_EQ(packRgba({nan, nan, nan, nan}), 0x00000000u);
}

TEST(sprite_forward_rotates_without_trig) {
    // {1, 0} is the identity, and it must be exactly the identity -- an
    // axis-aligned tile that lands a hair off its neighbour seams at any zoom.
    // Hence a zero tolerance rather than CHECK_EQ: -Wfloat-equal is on, and it
    // is right to be, so exactness is asked for as "off by nothing".
    const std::array<Vec2, 4> flat = quadCorners({10.0f, 4.0f}, {2.0f, 6.0f}, {1.0f, 0.0f});
    CHECK_NEAR(flat[0].x, 9.0f, 0.0f);
    CHECK_NEAR(flat[0].y, 1.0f, 0.0f);
    CHECK_NEAR(flat[2].x, 11.0f, 0.0f);
    CHECK_NEAR(flat[2].y, 7.0f, 0.0f);

    // A quarter turn: local +x points at +y, so the quad's width becomes its
    // screen height. Bottom-left goes to bottom-right.
    const std::array<Vec2, 4> turned = quadCorners({0.0f, 0.0f}, {2.0f, 6.0f}, {0.0f, 1.0f});
    CHECK_NEAR(turned[0].x, 3.0f, 1e-6f);
    CHECK_NEAR(turned[0].y, -1.0f, 1e-6f);
    CHECK_NEAR(turned[2].x, -3.0f, 1e-6f);
    CHECK_NEAR(turned[2].y, 1.0f, 1e-6f);

    // Rotation preserves the diagonal, whatever the direction.
    const Vec2 forward = normalized({0.3f, -0.9f});
    const std::array<Vec2, 4> arbitrary = quadCorners({-5.0f, 2.0f}, {2.0f, 6.0f}, forward);
    const Vec2 diagonal = arbitrary[2] - arbitrary[0];
    CHECK_NEAR(std::sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y),
               std::sqrt(2.0f * 2.0f + 6.0f * 6.0f), 1e-4f);

    // The corners a quad is built from are the corners its vertices carry, and
    // v0 is the top edge of the image: the decoder hands over the top row
    // first, so the bottom-left corner samples v1.
    const std::array<Vertex, 4> quad =
        quadVertices({0.0f, 0.0f}, {2.0f, 2.0f}, {1.0f, 0.0f}, {0.25f, 0.5f, 0.75f, 1.0f}, {});
    CHECK_NEAR(quad[0].u, 0.25f, 0.0f);
    CHECK_NEAR(quad[0].v, 1.0f, 0.0f);
    CHECK_NEAR(quad[2].u, 0.75f, 0.0f);
    CHECK_NEAR(quad[2].v, 0.5f, 0.0f);
}

TEST(orthographic_maps_camera_corners_to_clip_space) {
    // An off-centre, non-square camera, because a symmetric one would pass with
    // the translation terms missing entirely.
    constexpr Camera camera{-1.0f, 21.0f, -1.0f, 13.0f};
    const std::array<float, 16> m = orthographic(camera);

    const Vec2 bottomLeft = project(m, {camera.left, camera.bottom});
    CHECK_NEAR(bottomLeft.x, -1.0f, 1e-5f);
    CHECK_NEAR(bottomLeft.y, -1.0f, 1e-5f);

    const Vec2 topRight = project(m, {camera.right, camera.top});
    CHECK_NEAR(topRight.x, 1.0f, 1e-5f);
    CHECK_NEAR(topRight.y, 1.0f, 1e-5f);

    const Vec2 center = project(m, {10.0f, 6.0f});
    CHECK_NEAR(center.x, 0.0f, 1e-5f);
    CHECK_NEAR(center.y, 0.0f, 1e-5f);

    // The unused terms stay zero: a stray value in a column a 2D projection
    // never writes is a matrix that works until something reads w.
    CHECK_NEAR(m[10], -1.0f, 0.0f);
    CHECK_NEAR(m[15], 1.0f, 0.0f);
    CHECK_NEAR(m[1], 0.0f, 0.0f);
    CHECK_NEAR(m[4], 0.0f, 0.0f);
}
