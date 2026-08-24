// ktd_hashdump -- prints the simulation's state hash for every tick of a
// scripted run, one "tick<TAB>hash" line each.
//
// This is what the cross-platform CI job actually compares. Each OS builds and
// runs this, uploads its trace, and a compare job diffs them and names the
// first tick where they disagree. A single end-of-run checksum would tell you
// that macOS and Windows disagree; a per-tick trace tells you they diverged at
// tick 1,847, which is the difference between a fixable bug and a week of
// bisecting by hand.
//
// It links only ktd_game, so it builds in seconds without SDL and can run on
// any CI image including ones with no display.

#include "Scenario.h"

#include "game/World.h"

#include <cstdio>

int main() {
    ktd::World world(scenario::content(), scenario::kSeed);

    // Hex, zero-padded and fixed-width, so the traces diff cleanly line by line
    // and a divergence shows up as a single changed line rather than a shift.
    scenario::run(world, [](std::uint64_t tick, const ktd::World& w) {
        std::printf("%llu\t%016llx\n", static_cast<unsigned long long>(tick),
                    static_cast<unsigned long long>(w.checksum()));
    });

    return 0;
}
