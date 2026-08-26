#pragma once

// What happened this tick, as a list of plain records.
//
// Presentation needs to know that a tower fired so it can play a muzzle flash,
// and that an enemy died so it can throw sparks. The alternative to an event is
// diffing state between frames, which means presentation reimplementing the
// simulation's rules badly and getting it wrong the first time a kill and a
// spawn land on the same tick.
//
// **Events are write-only from the simulation's perspective.** Nothing in
// `game/` may read the queue back. The moment a rule branches on an event, the
// queue stops being a report and becomes a message bus with an evaluation
// order to audit -- and the tick's behaviour starts depending on how far
// presentation had got through draining it.
//
// They are hashed anyway. A run that reaches the same state for a different
// reason is a run with a bug in it, and this is the cheapest place to notice.

#include "game/Entity.h"
#include "game/Span.h"
#include "game/Types.h"

#include <cstdint>
#include <vector>

namespace ktd {

// Append only: the numeric value goes into the state hash and, in Phase 7, into
// a replay file.
enum class EventType : std::uint8_t {
    None = 0,
    WaveStarted = 1,
    EnemySpawned = 2,
    EnemyDamaged = 3,
    EnemyKilled = 4,
    EnemyLeaked = 5,
    TowerPlaced = 6,
    TowerRejected = 7,
    TowerFired = 8,
    StatusApplied = 9,
    Defeat = 10,
    Victory = 11,
};

// One record, flat and POD.
//
// A variant or an inheritance hierarchy would read better and serialize worse:
// this has a fixed size and a fixed field order, which is what lets the hash
// and the Phase 7 writer treat it like every other piece of state.
struct Event {
    EventType type = EventType::None;
    // The entity the event is about: the enemy that died, the tower that fired.
    // A stable EntityId, never a handle -- handles are storage addresses and
    // must not leave the simulation (ADR 0009).
    EntityId subject = kInvalidId;
    // Who caused it, where that is a different entity from the subject.
    EntityId source = kInvalidId;
    // A def id, a PlacementResult, a StatusDefId -- whatever the type needs.
    std::uint32_t detail = 0;
    // Where it happened, for the effect that draws there.
    Vec2 position{};
    // Damage dealt, gold paid, stacks applied.
    float value = 0.0f;
};

// One tick's events.
//
// Cleared at the top of every step(), so it holds exactly one tick and never
// grows without bound. That is also what keeps it hashable: an accumulating
// queue would make the checksum depend on how often the caller happened to
// drain it, which is a frame-rate dependency in the determinism trace.
class EventQueue {
public:
    void clear() { events_.clear(); }
    void reserve(std::size_t n) { events_.reserve(n); }
    void push(const Event& event) { events_.push_back(event); }

    Span<const Event> events() const { return Span<const Event>(events_); }
    std::size_t size() const { return events_.size(); }
    bool empty() const { return events_.empty(); }

    template <typename Sink>
    void serialize(Sink& out) const {
        out.u32(static_cast<std::uint32_t>(events_.size()));
        for (const Event& event : events_) {
            out.u8(static_cast<std::uint8_t>(event.type));
            out.u32(event.subject);
            out.u32(event.source);
            out.u32(event.detail);
            out.f32(event.position.x);
            out.f32(event.position.y);
            out.f32(event.value);
        }
    }

private:
    std::vector<Event> events_;
};

}  // namespace ktd
