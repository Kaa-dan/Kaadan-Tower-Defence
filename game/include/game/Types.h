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

// Deterministic RNG. Never use rand() in the simulation: replays, save/resume
// and cross-platform consistency all depend on the same seed producing the
// same run on every target.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) : state_(seed | 1u) {}

    std::uint32_t next() {
        // xorshift64*
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return static_cast<std::uint32_t>((state_ * 0x2545F4914F6CDD1Dull) >> 32);
    }

    float unit() { return static_cast<float>(next()) / 4294967296.0f; }
    float range(float lo, float hi) { return lo + unit() * (hi - lo); }
    int rangeInt(int lo, int hiInclusive) {
        return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hiInclusive - lo + 1));
    }

private:
    std::uint64_t state_;
};

}  // namespace ktd
