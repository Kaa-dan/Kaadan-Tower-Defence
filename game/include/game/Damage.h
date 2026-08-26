#pragma once

// The damage pipeline: one place where every point of damage in the game is
// resolved, and one written-down order of operations.
//
// Every source -- a projectile landing, a burn ticking, whatever Phase 8 adds --
// queues a DamageEvent rather than subtracting from health directly. That is
// what makes the formula auditable: there is exactly one function to read, and
// a new damage source cannot accidentally skip resistance because its author
// did not know resistance existed.
//
// See ADR 0011 for why the order is what it is.

#include "game/Entity.h"
#include "game/Status.h"
#include "game/content/Database.h"

#include <bit>
#include <cstdint>
#include <vector>

namespace ktd {

// Where a point of damage came from. Only used for attribution in events; the
// formula does not branch on it.
enum class DamageSource : std::uint8_t {
    Projectile = 0,
    Status = 1,
};

struct DamageEvent {
    // The handle for the O(1) lookup, and the id for canonical ordering. Same
    // two-names-for-one-thing argument as Projectile::target.
    EnemyId target{};
    EntityId targetId = kInvalidId;

    // Already through the flat and percent stages: see resolveStats(), which is
    // where the tower's half of the formula lives.
    float amount = 0.0f;
    content::DamageTypeId type = content::kNoDef;

    TowerId source{};
    EntityId sourceId = kInvalidId;
    DamageSource kind = DamageSource::Projectile;
};

// The final stages of the formula, applied to one enemy.
//
//     amount
//       x status multipliers, ascending StatusDefId, once per stack
//       x (1 - resistance to this damage type)
//       - armor
//       clamped at 0
//
// The status multipliers iterate **by def id, not by slot**. Slot order is the
// order effects happened to land, so multiplying in slot order would make
// "chill then hex" deal a different number from "hex then chill" -- float
// multiplication is not associative. That difference is small enough to survive
// review and large enough to desync a replay, and it is the exact shape of bug
// that reaches a forum as "my damage is inconsistent". Ascending def id removes
// it. See ADR 0011.
inline float resolveDamage(const DamageEvent& event, const Enemy& enemy,
                           const content::ContentDb& content) {
    float amount = event.amount;

    for (content::StatusDefId id = 0; id < content.statuses().size(); ++id) {
        const std::uint16_t stacks = statusStacks(enemy.statuses, id);
        if (stacks == 0) {
            continue;
        }
        const content::StatusDef& def = content.status(id);
        for (std::uint16_t i = 0; i < stacks; ++i) {
            amount *= def.damageTakenMultiplier;
        }
    }

    const content::EnemyDef& def = content.enemy(enemy.type);
    amount *= 1.0f - def.resistance(event.type);
    amount -= def.armor;
    return amount > 0.0f ? amount : 0.0f;
}

// The tick's pending damage, resolved together at a fixed point in the step.
//
// **The queue is sorted before it is drained**, and that is the whole reason it
// is a queue rather than a direct call. Projectiles are visited in pool order,
// and pool order is a function of destruction history (ADR 0009) -- so two
// shots landing on one enemy in the same tick would subtract in an order that
// depended on which projectile happened to die first. `(h - a) - b` and
// `(h - b) - a` are not the same float, so the enemy's remaining health, and
// therefore whether it dies this tick, would depend on storage layout.
//
// Sorting on (targetId, sourceId, kind, type, amount bits) is a total order
// over stable identities, so the drain order is the same on every machine and
// after every refactor of how entities are stored.
class DamageQueue {
public:
    void clear() { events_.clear(); }

    void reserve(std::size_t n) {
        events_.reserve(n);
        scratch_.reserve(n);
    }

    void push(const DamageEvent& event) { events_.push_back(event); }

    bool empty() const { return events_.empty(); }
    std::size_t size() const { return events_.size(); }

    // The canonical order. A total order over stable identities: no handle, no
    // array position, nothing that destruction history can move.
    static bool before(const DamageEvent& a, const DamageEvent& b) {
        if (a.targetId != b.targetId) {
            return a.targetId < b.targetId;
        }
        if (a.sourceId != b.sourceId) {
            return a.sourceId < b.sourceId;
        }
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        if (a.type != b.type) {
            return a.type < b.type;
        }
        // Bit comparison, not value comparison: it is a total order over the
        // NaNs a corrupt save could contain, where `<` on floats is not.
        return std::bit_cast<std::uint32_t>(a.amount) < std::bit_cast<std::uint32_t>(b.amount);
    }

    // Puts the queue in canonical order. Called once, immediately before the
    // drain.
    //
    // Hand-written rather than std::stable_sort, and the reason is allocation
    // rather than determinism.
    //
    // std::sort is banned in game/ outright -- the three standard libraries
    // pick different introsort pivots and order equal elements differently.
    // std::stable_sort would be correct here, but MSVC's implementation takes a
    // temporary buffer from the heap for any range past its insertion-sort
    // threshold, and this runs inside step(), which must not allocate. That is
    // not a portability footnote: `steady_state_step_allocates_nothing` caught
    // it on the Windows CI leg while macOS and its stack-buffered libc++ passed
    // clean.
    //
    // Bottom-up merge sort over a scratch buffer reserved once in the
    // constructor. Stable by construction -- ties take the left run first --
    // O(n log n), and identical on every platform because every comparison and
    // every merge step is written out here.
    void sortCanonically() {
        const std::size_t count = events_.size();
        if (count < 2) {
            return;
        }
        scratch_.resize(count);

        for (std::size_t width = 1; width < count; width *= 2) {
            for (std::size_t start = 0; start < count; start += width * 2) {
                const std::size_t middle = start + width < count ? start + width : count;
                const std::size_t end = start + width * 2 < count ? start + width * 2 : count;

                std::size_t left = start;
                std::size_t right = middle;
                for (std::size_t out = start; out < end; ++out) {
                    // `!before(right, left)` rather than `before(left, right)`:
                    // on a tie the left run wins, which is what makes this
                    // stable.
                    const bool takeLeft =
                        left < middle && (right >= end || !before(events_[right], events_[left]));
                    scratch_[out] = takeLeft ? events_[left++] : events_[right++];
                }
            }
            events_.swap(scratch_);
        }
    }

    Span<const DamageEvent> events() const { return Span<const DamageEvent>(events_); }

private:
    std::vector<DamageEvent> events_;
    // Reserved alongside events_ and reused, never freed between ticks.
    std::vector<DamageEvent> scratch_;
};

}  // namespace ktd
