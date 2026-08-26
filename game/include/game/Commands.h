#pragma once

// Player intent, queued rather than applied.
//
// The UI used to call World::placeTower() straight from the click handler and
// read the answer back from the return value. That works, and it makes the
// replay log a second thing that has to be remembered about separately -- every
// new player action needs a matching "and also write it down" that someone will
// forget.
//
// A command queue collapses the two. **The replay log is exactly this command
// stream plus tick numbers**, because it is the only way anything gets into the
// simulation from outside. Phase 7's replay writer is therefore a file format
// and nothing else; there is no second path to audit.
//
// The outcome comes back as an event rather than a return value, which is what
// makes the two directions symmetrical: intent in, facts out.

#include "game/Types.h"
#include "game/content/Database.h"

#include <cstdint>
#include <vector>

namespace ktd {

// Append only: the value is what a replay file records.
enum class CommandType : std::uint8_t {
    PlaceTower = 1,
};

struct Command {
    CommandType type = CommandType::PlaceTower;
    Cell cell{};
    content::TowerDefId tower = 0;
};

enum class PlacementResult : std::uint8_t {
    Placed = 0,
    BlockedTile = 1,    // wall, existing tower, spawn or goal
    WouldSealPath = 2,  // legal tile, but it would wall the route off entirely
    NotEnoughGold = 3,
    UnknownTowerType = 4,  // the content has no tower with that index
};

// More commands than a human can produce in one 1/60s tick, by a wide margin.
// Beyond it the enqueue is refused rather than the buffer grown, because a
// growing buffer is an allocation on a gameplay path and an unbounded one is a
// way for a malformed replay file to exhaust memory.
inline constexpr std::size_t kMaxPendingCommands = 64;

class CommandQueue {
public:
    void reserve(std::size_t n) { pending_.reserve(n); }

    // False when the queue is full.
    bool push(const Command& command) {
        if (pending_.size() >= kMaxPendingCommands) {
            return false;
        }
        pending_.push_back(command);
        return true;
    }

    void clear() { pending_.clear(); }
    Span<const Command> pending() const { return Span<const Command>(pending_); }
    std::size_t size() const { return pending_.size(); }
    bool empty() const { return pending_.empty(); }

    template <typename Sink>
    void serialize(Sink& out) const {
        out.u32(static_cast<std::uint32_t>(pending_.size()));
        for (const Command& command : pending_) {
            out.u8(static_cast<std::uint8_t>(command.type));
            out.i32(command.cell.x);
            out.i32(command.cell.y);
            out.u32(command.tower);
        }
    }

private:
    std::vector<Command> pending_;
};

}  // namespace ktd
