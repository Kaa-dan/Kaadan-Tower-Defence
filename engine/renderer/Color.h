#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>

namespace ktd {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

}  // namespace ktd

namespace ktd::render {

// The public API talks in four floats; the vertex buffer stores four bytes.
// Four bytes per vertex instead of sixteen is a third of the bandwidth for a
// value no monitor can resolve past 8 bits anyway.
//
// The clamp is not defensive tidiness. A float -> integer narrowing conversion
// whose value does not fit the destination is undefined behaviour, and tints
// here are computed, not authored: a health fraction is one zero maxHealth away
// from an infinity and a damage flash is one overshoot away from 1.4. This is
// exactly the class of bug the asan preset exists to catch, so it is cheaper
// not to write it. NaN is folded to 0 by ordering the clamp so that the
// comparison against the low bound fails.
inline constexpr std::uint32_t packRgba(Color color) {
    // Byte order is r, g, b, a ascending in memory, which is what a
    // GL_UNSIGNED_BYTE / 4-component / normalized attribute reads.
    static_assert(std::endian::native == std::endian::little,
                  "the packed vertex colour assumes little-endian byte order");

    const auto channel = [](float value) -> std::uint32_t {
        const float clamped = value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
        return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
    };
    return channel(color.r) | (channel(color.g) << 8) | (channel(color.b) << 16) |
           (channel(color.a) << 24);
}

}  // namespace ktd::render
