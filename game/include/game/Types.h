#pragma once

#include "game/Math.h"

#include <cstdint>

namespace ktd {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float lengthSq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return math::sqrt(lengthSq(v)); }

inline Vec2 normalized(Vec2 v) {
    const float len = length(v);
    return len > 1e-6f ? v * (1.0f / len) : Vec2{0.0f, 0.0f};
}

// Integer tile coordinate.
struct Cell {
    int x = 0;
    int y = 0;
};

inline bool operator==(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Cell a, Cell b) { return !(a == b); }

}  // namespace ktd
