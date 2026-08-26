#pragma once

// The stat-modifier layer: how a skill node or an item changes a tower.
//
// ADR 0003 rules out embedded scripting, so the design space for 100+
// combinable items has to be spanned by a small set of code-level verbs plus
// numbers. This is the numeric half of that budget: a mod names a stat, an
// operation and a value, and nothing else. The verbs (splash, pierce, chain,
// on-kill) are the other half and are not written yet -- nothing consumes them,
// and Phase 3's lesson was that an empty struct written early fossilises a
// guess.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace ktd {

// Which number a mod moves. Append only: the value is what a save file and the
// state hash record.
enum class StatId : std::uint8_t {
    Damage = 0,
    Range = 1,
    ReloadTicks = 2,
    Count,
};

inline constexpr std::size_t kStatCount = static_cast<std::size_t>(StatId::Count);

enum class ModOp : std::uint8_t {
    Flat = 0,     // added to the base
    Percent = 1,  // summed, then applied once as (1 + total)
};

struct StatMod {
    StatId stat = StatId::Damage;
    ModOp op = ModOp::Flat;
    float value = 0.0f;
};

// More mods than any tower is expected to carry, held inline. See Entity.h for
// why this is not a std::vector.
inline constexpr std::size_t kModSlots = 8;

// A tower's mods, and the count of how many of the slots are live.
//
// Deliberately a plain aggregate rather than a class: it is serialized field by
// field like everything else, and a class with an invariant would tempt someone
// to make `add` do something clever.
struct ModSet {
    std::array<StatMod, kModSlots> mods{};
    std::uint8_t count = 0;

    // Returns false when the set is full. Dropping is deterministic; growing
    // would put an allocation on a gameplay path.
    bool add(StatMod mod) {
        if (count >= kModSlots) {
            return false;
        }
        mods[count++] = mod;
        return true;
    }

    void clear() { count = 0; }
};

// The numbers a tower actually fights with this tick.
struct TowerStats {
    float damage = 0.0f;
    float range = 0.0f;
    std::uint32_t reloadTicks = 0;
};

// Resolve a base stat line against a mod set.
//
// The first two stages of the damage formula (ADR 0011) live here:
//
//     base -> + flat mods -> x (1 + summed percent)
//
// **The mods are summed in a canonical order, not in insertion order.** Float
// addition is not associative, so `(base + a) + b` and `(base + b) + a` can
// differ in the last bit -- which would make a tower's damage depend on the
// order the player happened to socket two items, and make two replays of the
// same run disagree. Sorting by (stat, op, value bits) removes the dependency
// entirely rather than making it merely unlikely.
//
// This is the same argument as the ascending-StatusDefId rule on the other side
// of the formula, applied to the tower's half.
inline TowerStats resolveStats(TowerStats base, const ModSet& set) {
    std::array<StatMod, kModSlots> ordered = set.mods;
    const std::size_t live = std::min<std::size_t>(set.count, kModSlots);
    // stable_sort, not sort: the purity guard bans std::sort, because the three
    // standard libraries pick different introsort pivots.
    std::stable_sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(live),
                     [](const StatMod& a, const StatMod& b) {
                         if (a.stat != b.stat) {
                             return a.stat < b.stat;
                         }
                         if (a.op != b.op) {
                             return a.op < b.op;
                         }
                         // Bit comparison, not value comparison: two mods with
                         // the same numeric value are interchangeable, and
                         // comparing bits gives a total order over the NaNs a
                         // corrupt save could contain.
                         return std::bit_cast<std::uint32_t>(a.value) <
                                std::bit_cast<std::uint32_t>(b.value);
                     });

    std::array<float, kStatCount> flat{};
    std::array<float, kStatCount> percent{};
    for (std::size_t i = 0; i < live; ++i) {
        const StatMod& mod = ordered[i];
        const std::size_t index = static_cast<std::size_t>(mod.stat);
        if (index >= kStatCount) {
            continue;
        }
        if (mod.op == ModOp::Flat) {
            flat[index] += mod.value;
        } else {
            percent[index] += mod.value;
        }
    }

    const auto apply = [&](float value, StatId stat) {
        const std::size_t index = static_cast<std::size_t>(stat);
        return (value + flat[index]) * (1.0f + percent[index]);
    };

    TowerStats out;
    out.damage = std::max(0.0f, apply(base.damage, StatId::Damage));
    out.range = std::max(0.0f, apply(base.range, StatId::Range));
    // Reload is a tick count, so it rounds rather than truncating and never
    // reaches zero -- a tower that fires every tick is a tower that fires
    // sixty times a second.
    const float reload = apply(static_cast<float>(base.reloadTicks), StatId::ReloadTicks);
    out.reloadTicks = reload <= 1.0f ? 1u : static_cast<std::uint32_t>(reload + 0.5f);
    return out;
}

template <typename Sink>
void serializeMods(Sink& out, const ModSet& set) {
    const std::uint8_t live =
        static_cast<std::uint8_t>(std::min<std::size_t>(set.count, kModSlots));
    out.u8(live);
    for (std::size_t i = 0; i < live; ++i) {
        out.u8(static_cast<std::uint8_t>(set.mods[i].stat));
        out.u8(static_cast<std::uint8_t>(set.mods[i].op));
        out.f32(set.mods[i].value);
    }
}

}  // namespace ktd
