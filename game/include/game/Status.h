#pragma once

// Status effects: burn, chill, vulnerability -- anything that sits on an enemy
// for a while and changes what happens to it.
//
// The definitions (how long, how much, how they stack) are content; this header
// is the runtime half: the slots an enemy carries and the rules for putting
// something into them.

#include "game/content/Database.h"

#include <array>
#include <cstdint>

namespace ktd {

// Eight slots, inline on the enemy. See Entity.h for why this is not a
// std::vector.
inline constexpr std::size_t kStatusSlots = 8;

// One live effect. `def` is kNoDef when the slot is empty.
struct StatusSlot {
    content::StatusDefId def = content::kNoDef;
    std::uint16_t stacks = 0;
    std::uint32_t remaining = 0;  // ticks
};

using StatusSlots = std::array<StatusSlot, kStatusSlots>;

enum class StatusApply : std::uint8_t {
    Added,      // occupied a free slot
    Stacked,    // an existing instance grew
    Refreshed,  // an existing instance had its duration reset
    Ignored,    // already present, and the policy says leave it alone
    NoRoom,     // every slot is taken by another effect
};

// Applies one instance of `def` to `slots`.
//
// At most one slot ever holds a given def, which is what makes the "ascending
// StatusDefId" rule in the damage formula a total order: there is nothing to
// break a tie between.
//
// A full slot table drops the effect rather than evicting one. Eviction needs a
// priority, a priority is a design decision nobody has made, and dropping is at
// least honest -- and deterministic, which is the property that matters here.
inline StatusApply applyStatus(StatusSlots& slots, const content::StatusDef& def,
                               content::StatusDefId id) {
    for (StatusSlot& slot : slots) {
        if (slot.def != id) {
            continue;
        }
        switch (def.policy) {
            case content::StackPolicy::Ignore:
                return StatusApply::Ignored;
            case content::StackPolicy::Refresh:
                slot.remaining = def.durationTicks;
                return StatusApply::Refreshed;
            case content::StackPolicy::StackIntensity:
                if (slot.stacks < def.maxStacks) {
                    ++slot.stacks;
                }
                // The intensity grows and the clock restarts: a stack that
                // aged out while the next one landed would make the effect's
                // strength depend on frame timing rather than on how often it
                // was applied.
                slot.remaining = def.durationTicks;
                return StatusApply::Stacked;
            case content::StackPolicy::StackDuration: {
                const std::uint32_t cap = def.durationTicks * def.maxStacks;
                const std::uint32_t grown = slot.remaining + def.durationTicks;
                slot.remaining = grown > cap ? cap : grown;
                return StatusApply::Stacked;
            }
        }
    }

    for (StatusSlot& slot : slots) {
        if (slot.def != content::kNoDef) {
            continue;
        }
        slot.def = id;
        slot.stacks = 1;
        slot.remaining = def.durationTicks;
        return StatusApply::Added;
    }
    return StatusApply::NoRoom;
}

// How many stacks of `id` are live, or 0.
inline std::uint16_t statusStacks(const StatusSlots& slots, content::StatusDefId id) {
    for (const StatusSlot& slot : slots) {
        if (slot.def == id) {
            return slot.stacks;
        }
    }
    return 0;
}

// The product of every live effect's speed multiplier, in ascending
// StatusDefId order.
//
// Same argument as the damage multipliers: float multiplication is not
// associative, and slot order is a function of the order effects happened to
// land. Iterating by def id instead makes a chilled-then-hexed enemy move at
// exactly the speed of a hexed-then-chilled one.
inline float statusSpeedMultiplier(const StatusSlots& slots, const content::ContentDb& content) {
    float multiplier = 1.0f;
    for (content::StatusDefId id = 0; id < content.statuses().size(); ++id) {
        const std::uint16_t stacks = statusStacks(slots, id);
        if (stacks == 0) {
            continue;
        }
        const content::StatusDef& def = content.status(id);
        for (std::uint16_t i = 0; i < stacks; ++i) {
            multiplier *= def.speedMultiplier;
        }
    }
    return multiplier;
}

template <typename Sink>
void serializeStatuses(Sink& out, const StatusSlots& slots) {
    for (const StatusSlot& slot : slots) {
        out.u32(slot.def);
        out.u32(slot.stacks);
        out.u32(slot.remaining);
    }
}

}  // namespace ktd
