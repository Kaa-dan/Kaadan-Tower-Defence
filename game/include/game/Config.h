#pragma once

#include <cstdint>

namespace ktd::config {

// All balance tunables live here so design changes never require hunting
// through simulation code.

// --- Simulation ---------------------------------------------------------
inline constexpr int kSimulationHz = 60;
inline constexpr float kFixedDt = 1.0f / static_cast<float>(kSimulationHz);

// Durations are authored in seconds because that is how they are designed, and
// stored as tick counts because that is how they are counted. A float timer
// decremented by dt accumulates rounding error that depends on how many times
// it has been decremented, which makes "6 seconds between waves" mean something
// slightly different on the 200th wave than on the first -- and gives the
// optimiser latitude to make it differ per platform. An integer tick count is
// exact by construction.
constexpr std::uint32_t ticksFromSeconds(float seconds) {
    return static_cast<std::uint32_t>(seconds * static_cast<float>(kSimulationHz) + 0.5f);
}

// --- Level --------------------------------------------------------------
inline constexpr int kGridWidth = 24;
inline constexpr int kGridHeight = 16;

// --- Economy ------------------------------------------------------------
inline constexpr int kStartingGold = 120;
inline constexpr int kStartingLives = 20;
inline constexpr int kTowerCost = 40;
inline constexpr int kGoldPerKill = 8;

// --- Towers -------------------------------------------------------------
inline constexpr float kTowerRange = 3.2f;  // tiles
inline constexpr float kTowerDamage = 10.0f;
// Authored as a reload time in ticks rather than a rate, because the rate does
// not divide the tick evenly: 1.6 shots/second is 37.5 ticks, so the real rate
// is 1.579/second.
//
// This is NOT a balance change, despite what the commit that introduced it
// claimed. The old float timer started at 0.625f and needed 38 decrements of
// 1/60f to go non-positive, so towers fired every 38 ticks before the
// conversion too. See the note on kSpawnIntervalTicks for the two timers that
// did change.
inline constexpr std::uint32_t kTowerReloadTicks = 38;

// --- Projectiles --------------------------------------------------------
inline constexpr float kProjectileSpeed = 9.0f;  // tiles/second
inline constexpr float kProjectileHitRadius = 0.18f;
inline constexpr float kProjectileLifetime = 3.0f;  // seconds
// 180, where the float timer expired at 181 -- projectiles live one tick less
// and reach about 0.15 tiles less far. See kSpawnIntervalTicks.
inline constexpr std::uint32_t kProjectileLifetimeTicks = ticksFromSeconds(kProjectileLifetime);

// --- Enemies ------------------------------------------------------------
inline constexpr float kEnemyBaseHealth = 24.0f;
inline constexpr float kEnemyHealthGrowth = 1.18f;  // per wave, compounding
inline constexpr float kEnemyBaseSpeed = 1.6f;      // tiles/second
inline constexpr float kEnemySpeedGrowth = 1.02f;
inline constexpr float kEnemyRadius = 0.22f;

// --- Waves --------------------------------------------------------------
inline constexpr int kEnemiesInFirstWave = 6;
inline constexpr int kEnemiesAddedPerWave = 2;
inline constexpr float kSpawnInterval = 0.75f;  // seconds between spawns
inline constexpr float kTimeBetweenWaves = 6.0f;
// The tick conversion shifted two timers by one tick each, and it is worth
// being exact about which, because the phase commit that made the change named
// the wrong one.
//
// The old code decremented a float by 1/60f and fired when it went non-positive.
// Summing 45 of those lands just under 0.75, so the float timer needed a 46th
// tick; 180 increments reach just under 3.0, so projectiles needed a 181st.
// kTimeBetweenWaves happened to accumulate the other way and landed exactly on
// 360, and the tower reload was already 38.
//
//   spawn interval    46 -> 45   waves arrive ~2.2% faster
//   projectile life  181 -> 180  slightly less reach
//   wave gap         360 -> 360  unchanged
//   tower reload      38 -> 38   unchanged
//
// That two identical-looking float timers rounded in opposite directions is
// precisely the accumulated-error hazard the tick conversion existed to remove,
// so the new numbers are the correct ones. They are still a gameplay change and
// deserve re-tuning rather than inheritance.
inline constexpr std::uint32_t kSpawnIntervalTicks = ticksFromSeconds(kSpawnInterval);
inline constexpr std::uint32_t kTimeBetweenWavesTicks = ticksFromSeconds(kTimeBetweenWaves);

}  // namespace ktd::config
