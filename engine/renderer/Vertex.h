#pragma once

#include "game/Types.h"
#include "renderer/Color.h"

#include <array>
#include <cstdint>

namespace ktd::render {

// Identifies an uploaded texture. Opaque on purpose: it is a GL name today and
// a Metal texture index in the port, and nothing above backends/ may assume
// which.
//
// 0 is the 1x1 opaque white texture every backend creates at init, so an
// untextured quad is a textured quad and there is exactly one draw path.
using TextureId = std::uint32_t;

inline constexpr TextureId kWhiteTexture = 0;

// A sub-rectangle of a texture in normalized coordinates.
struct UvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

// 20 bytes: position, texture coordinate, packed colour. The previous format
// was six floats with no room for a UV; this adds texturing and still costs
// four bytes less per vertex.
struct Vertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t rgba = 0xFFFFFFFFu;
};

static_assert(sizeof(Vertex) == 20, "the vertex attribute strides assume a packed 20-byte vertex");

// Corner order, and the only winding the index buffer knows: bottom-left,
// bottom-right, top-right, top-left.
inline constexpr std::array<std::uint32_t, 6> kQuadIndices{0, 1, 2, 0, 2, 3};

// The four corners of a quad rotated so that its local +x axis points along
// `forward`, which must be unit length. `{1, 0}` is unrotated.
//
// A direction rather than an angle, and this function is why (ADR 0015). The
// simulation already maintains `tower.aim` as a unit vector, so an angle API
// would make the caller atan2 a vector it had and make this code sin/cos it
// straight back -- two transcendental round trips to recover the input, and an
// invitation to put trig into game/Math.h where purity_determinism bans it. A
// unit forward is a 2x2 rotation with no trig at all.
inline constexpr std::array<Vec2, 4> quadCorners(Vec2 center, Vec2 size, Vec2 forward) {
    const float halfW = size.x * 0.5f;
    const float halfH = size.y * 0.5f;

    const auto corner = [&](float x, float y) -> Vec2 {
        return {center.x + x * forward.x - y * forward.y, center.y + x * forward.y + y * forward.x};
    };
    return {corner(-halfW, -halfH), corner(halfW, -halfH), corner(halfW, halfH),
            corner(-halfW, halfH)};
}

// The same four corners with the texture coordinates that go with them. `v0`
// is the bottom edge: images are decoded top-row-first and uploaded as-is, so
// the flip lives here, once, instead of in every caller's UV maths.
inline constexpr std::array<Vertex, 4> quadVertices(Vec2 center, Vec2 size, Vec2 forward, UvRect uv,
                                                    Color tint) {
    const std::array<Vec2, 4> p = quadCorners(center, size, forward);
    const std::uint32_t rgba = packRgba(tint);
    return {Vertex{p[0].x, p[0].y, uv.u0, uv.v1, rgba}, Vertex{p[1].x, p[1].y, uv.u1, uv.v1, rgba},
            Vertex{p[2].x, p[2].y, uv.u1, uv.v0, rgba}, Vertex{p[3].x, p[3].y, uv.u0, uv.v0, rgba}};
}

}  // namespace ktd::render
