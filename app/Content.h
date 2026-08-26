#pragma once

// Reading the content directory, and re-simulating a run against a fresh copy
// of it.
//
// This lives in app/ rather than game/ because it opens files, and the whole
// portability argument rests on the simulation never doing that. game/ parses
// strings; the platform hands it the bytes.

#include "game/World.h"
#include "game/content/Database.h"
#include "platform/Platform.h"

#include <string>
#include <vector>

namespace ktd::app {

// One player action and the tick it landed on.
//
// The tick is what makes the log replayable. Input arrives at frame rate and
// the simulation advances at a fixed rate, so "the click that happened during
// this frame" is not a well-defined point in the run unless the tick is
// recorded with it.
//
// The action itself is a ktd::Command -- the same type the simulation accepts,
// not a parallel description of it. Phase 4 made the command queue the only way
// in, so the log is that stream plus tick numbers and there is no second path
// that could disagree with it.
struct RecordedCommand {
    std::uint64_t tick = 0;
    Command command{};
};

// The content directory, and everything needed to reload it.
class ContentSource {
public:
    explicit ContentSource(std::string directory) : directory_(std::move(directory)) {}

    // Reads and parses every content file into a FRESH database.
    //
    // On failure `out` is left untouched and the reasons are in errors(), so
    // the caller can keep running on the database it already had. Half the
    // times F5 is pressed the file has a syntax error in it, and losing the run
    // to a missing quote would make the feature unusable.
    bool reload(const Platform& platform, content::ContentDb& out);

    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::string directory_;
    std::vector<std::string> errors_;
};

// Replays `commands` against `world` from tick 0 up to `throughTick`.
//
// This is what hot reload actually does. "New numbers from here on" would leave
// a board built under the old balance and tell you nothing about the change;
// re-running the whole recorded run against the new data answers the question
// the designer is actually asking, which is "show me this run, rebalanced".
// Thirty minutes of play resimulates in well under a second.
void resimulate(World& world, const std::vector<RecordedCommand>& commands,
                std::uint64_t throughTick);

}  // namespace ktd::app
