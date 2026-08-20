#pragma once

namespace ktd::config {

// All balance tunables live here so design changes never require hunting
// through simulation code.

// --- Simulation ---------------------------------------------------------
inline constexpr float kFixedDt = 1.0f / 60.0f;  // simulation Hz

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
inline constexpr float kTowerShotsPerSecond = 1.6f;

// --- Projectiles --------------------------------------------------------
inline constexpr float kProjectileSpeed = 9.0f;  // tiles/second
inline constexpr float kProjectileHitRadius = 0.18f;
inline constexpr float kProjectileLifetime = 3.0f;

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

}  // namespace ktd::config
