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
// not divide the tick evenly: 1.6 shots/second is 37.5 ticks. Rounding to 38
// ticks makes the real rate 1.579/second. See the commit that introduced this.
inline constexpr std::uint32_t kTowerReloadTicks = 38;

// --- Projectiles --------------------------------------------------------
inline constexpr float kProjectileSpeed = 9.0f;  // tiles/second
inline constexpr float kProjectileHitRadius = 0.18f;
inline constexpr float kProjectileLifetime = 3.0f;  // seconds
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
inline constexpr std::uint32_t kSpawnIntervalTicks = ticksFromSeconds(kSpawnInterval);
inline constexpr std::uint32_t kTimeBetweenWavesTicks = ticksFromSeconds(kTimeBetweenWaves);

}  // namespace ktd::config
