#pragma once

#include <cstdint>

// What is left of the old balance header.
//
// Every tunable that used to live here -- tower range and damage, enemy health
// and growth, wave sizes, economy, level geometry -- moved into content files
// in Phase 3 and is read through content::ContentDb. That was the point: a
// constant in a header is a constant only a programmer can change, and it makes
// a second tower type a code change instead of an afternoon of data editing.
//
// What remains is not balance. It is the simulation's clock, which is a
// property of the engine rather than a knob for the designer: changing it
// changes what every recorded replay means.

namespace ktd::config {

inline constexpr int kSimulationHz = 60;
inline constexpr float kFixedDt = 1.0f / static_cast<float>(kSimulationHz);

// Content authors durations directly in ticks, because the simulation counts
// ticks and a duration that is not a whole number of them cannot survive the
// conversion (ADR 0007). This is kept for the places that reason about the
// relationship -- and as the one honest way to write a tick count derived from
// a design intent expressed in seconds.
constexpr std::uint32_t ticksFromSeconds(float seconds) {
    return static_cast<std::uint32_t>(seconds * static_cast<float>(kSimulationHz) + 0.5f);
}

}  // namespace ktd::config
