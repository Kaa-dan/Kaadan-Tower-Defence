#pragma once

// One scripted run, shared by the golden-hash test and the ktd_hashdump tool.
//
// Both need to drive the simulation through the same sequence, and if each kept
// its own copy they would drift -- at which point the trace CI compares across
// three operating systems would no longer be the trace the golden test pins.
//
// The script exists because an unattended world is a weak test: lives run out
// within a couple of waves, step() starts returning early, and the rest of the
// trace is a constant. Placing towers keeps the projectile and damage paths
// live for the whole run.

#include "Content.h"

#include "game/World.h"

#include <array>
#include <cstdint>

namespace scenario {

// The scripted run is played against the shipped content, so the golden hash
// covers the real balance numbers rather than a fixture that could drift from
// them. A content edit therefore moves the hash -- deliberately, because it is
// a different run.
inline const ktd::content::ContentDb& content() { return testing::shippedContent(); }

inline constexpr std::uint64_t kSeed = 20260820;
inline constexpr std::uint64_t kTicks = 3000;

struct Command {
    std::uint64_t tick;
    ktd::Cell cell;
};

// Placements flank the open corridor at y = 8 rather than sitting on it, so the
// route is never sealed and the run does not depend on the path-blocking
// rules. The later ones are funded by kill income; if a wave goes badly they
// are refused for lack of gold, which is itself deterministic.
inline constexpr std::array<Command, 6> kCommands = {{
    {10, {5, 7}},
    {20, {5, 9}},
    {30, {11, 7}},
    {900, {11, 9}},
    {1500, {19, 7}},
    {2100, {19, 9}},
}};

// Runs the script, calling onTick(tick, world) after every step.
template <typename OnTick>
void run(ktd::World& world, OnTick&& onTick) {
    for (std::uint64_t tick = 1; tick <= kTicks; ++tick) {
        // Commands apply before the step, the way the app applies input before
        // its fixed-step loop.
        for (const Command& command : kCommands) {
            if (command.tick == tick) {
                (void)world.placeTower(command.cell);
            }
        }
        world.step();
        onTick(tick, world);
    }
}

}  // namespace scenario
