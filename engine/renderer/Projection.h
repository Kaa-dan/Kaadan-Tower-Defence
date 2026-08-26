#pragma once

#include "game/Types.h"
#include "renderer/Batch.h"

#include <array>

namespace ktd::render {

// Column-major orthographic projection, no depth -- this is 2D.
//
// It lives here rather than in the GL backend because it is arithmetic over
// four floats with one correct answer, and a wrong projection is a black
// screen with nothing to read. Here a test can state the property directly:
// the camera's corners land on the corners of clip space.
inline constexpr std::array<float, 16> orthographic(Camera camera) {
    std::array<float, 16> m{};
    m[0] = 2.0f / (camera.right - camera.left);
    m[5] = 2.0f / (camera.top - camera.bottom);
    m[10] = -1.0f;
    m[12] = -(camera.right + camera.left) / (camera.right - camera.left);
    m[13] = -(camera.top + camera.bottom) / (camera.top - camera.bottom);
    m[15] = 1.0f;
    return m;
}

// `point` through `m`, for tests and for picking. Only the four terms a 2D
// orthographic matrix can actually contribute.
inline constexpr Vec2 project(const std::array<float, 16>& m, Vec2 point) {
    return {m[0] * point.x + m[12], m[5] * point.y + m[13]};
}

}  // namespace ktd::render
