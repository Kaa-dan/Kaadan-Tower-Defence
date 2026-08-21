#pragma once

#include "game/Span.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace ktd {

// Generation-tagged handle into a Pool.
//
// 20 bits of slot index, 12 bits of generation. A handle to a destroyed entity
// fails its generation check and resolves to nullptr instead of dangling, which
// is the entire reason this type exists: the previous code returned a raw
// `const Enemy*` into a std::vector, and the first area-damage tower -- one that
// kills more than one enemy in a step -- would have made that pointer dangle.
//
// Tag makes the handle a distinct type per entity kind, so a TowerId cannot be
// passed where an EnemyId belongs. It costs nothing at runtime and turns a class
// of silent lookup bug into a compile error.
//
// Handles are RUNTIME ADDRESSES, NOT IDENTITIES. A handle names a storage slot,
// and which slot an entity lands in is a function of the pool's destruction
// history. So a handle must never be serialized, and must never decide a
// gameplay tie-break -- both would make behaviour depend on destruction order,
// which is the coupling ADR 0004 exists to remove. The stable `id` field on the
// entity is what the hash and the tie-breaks use. See ADR 0009.
template <typename Tag>
struct Handle {
    std::uint32_t bits = 0;  // 0 is never a live handle: generations start at 1

    static constexpr std::uint32_t kIndexBits = 20;
    static constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1u;
    static constexpr std::uint32_t kMaxGeneration = (1u << 12) - 1u;  // 4095

    static constexpr Handle make(std::uint32_t index, std::uint32_t generation) {
        return Handle{(generation << kIndexBits) | (index & kIndexMask)};
    }

    constexpr std::uint32_t index() const { return bits & kIndexMask; }
    constexpr std::uint32_t generation() const { return bits >> kIndexBits; }
    constexpr bool valid() const { return bits != 0; }
};

template <typename Tag>
constexpr bool operator==(Handle<Tag> a, Handle<Tag> b) {
    return a.bits == b.bits;
}
template <typename Tag>
constexpr bool operator!=(Handle<Tag> a, Handle<Tag> b) {
    return !(a == b);
}

// Dense storage with a sparse slot table and a FIFO free list.
//
// T must have a `std::uint32_t id` member. The pool stamps it from a monotonic
// per-pool counter on create(), and that id -- not the handle, not the array
// position -- is the entity's stable identity for hashing, saving and
// tie-breaking.
//
// The free list is FIFO rather than LIFO on purpose. LIFO hands the same slot
// straight back on the next create, so a handful of slots absorb every
// create/destroy pair and burn through their 4,096 generations quickly; FIFO
// cycles the whole table, so a wrap needs the entire pool's worth of churn
// first. A wrap is still handled correctly -- see reuse below -- it just becomes
// rare enough that the correctness path is the belt rather than the braces.
template <typename T, typename Tag>
class Pool {
public:
    using Id = Handle<Tag>;

    // The debug-only borrow guard.
    //
    // The dangling-pointer class this pool exists to kill does not disappear
    // just because lookups return a checked pointer: code can still call get(),
    // then destroy something, then dereference. The pointer is into the dense
    // array, so a swap-and-pop moved whatever it names. A Ref remembers the
    // pool's mutation epoch and re-checks it on every dereference, so that
    // mistake fails immediately and loudly in a debug build instead of reading a
    // neighbouring entity in a release one.
    class Ref {
    public:
        Ref() = default;
        Ref(T* item, const Pool* pool)
            : item_(item),
              pool_(pool),
              epoch_(pool != nullptr ? pool->epoch() : 0) {}

        explicit operator bool() const { return item_ != nullptr; }
        T* operator->() const { return checked(); }
        T& operator*() const { return *checked(); }
        T* get() const { return checked(); }

    private:
        T* checked() const {
            // Held across a create or destroy: the dense array may have moved
            // under it. Reacquire the pointer from the handle instead.
            assert((item_ == nullptr || pool_->epoch() == epoch_) &&
                   "pool Ref used after the pool was mutated");
            return item_;
        }

        T* item_ = nullptr;
        const Pool* pool_ = nullptr;
        std::uint64_t epoch_ = 0;
    };

    Id create(const T& value) {
        const std::uint32_t slotIndex = acquireSlot();
        Slot& slot = slots_[slotIndex];

        slot.dense = static_cast<std::uint32_t>(dense_.size());
        slot.live = true;

        dense_.push_back(value);
        dense_.back().id = nextSequence_++;
        denseToSlot_.push_back(slotIndex);

        ++epoch_;
        return Id::make(slotIndex, slot.generation);
    }

    bool destroy(Id handle) {
        Slot* slot = liveSlot(handle);
        if (slot == nullptr) {
            return false;
        }

        // Swap-and-pop, then repair the slot that owned the moved-in entity.
        const std::uint32_t hole = slot->dense;
        const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1);
        if (hole != last) {
            dense_[hole] = dense_[last];
            denseToSlot_[hole] = denseToSlot_[last];
            slots_[denseToSlot_[hole]].dense = hole;
        }
        dense_.pop_back();
        denseToSlot_.pop_back();

        slot->live = false;
        // Advance past 0 on wrap, so a recycled slot can never mint a handle
        // whose bits are zero -- that is the invalid handle.
        slot->generation = slot->generation == Id::kMaxGeneration ? 1u : slot->generation + 1u;
        releaseSlot(handle.index());

        ++epoch_;
        return true;
    }

    T* get(Id handle) {
        Slot* slot = liveSlot(handle);
        return slot != nullptr ? &dense_[slot->dense] : nullptr;
    }

    const T* get(Id handle) const {
        const Slot* slot = liveSlot(handle);
        return slot != nullptr ? &dense_[slot->dense] : nullptr;
    }

    Ref borrow(Id handle) { return Ref(get(handle), this); }

    // The handle naming the entity at dense position i.
    //
    // The bridge between iterating (which is by dense position) and remembering
    // (which must be by handle). Code that scans items() and wants to destroy
    // something has to convert here rather than hold the index, because the
    // first destroy invalidates every later index.
    Id handleAt(std::size_t i) const {
        const std::uint32_t slot = denseToSlot_[i];
        return Id::make(slot, slots_[slot].generation);
    }

    bool alive(Id handle) const { return liveSlot(handle) != nullptr; }

    Span<T> items() { return Span<T>(dense_.data(), dense_.size()); }
    Span<const T> items() const { return Span<const T>(dense_.data(), dense_.size()); }

    std::size_t size() const { return dense_.size(); }
    bool empty() const { return dense_.empty(); }

    void reserve(std::size_t n) {
        dense_.reserve(n);
        denseToSlot_.reserve(n);
        slots_.reserve(n);
    }

    // Bumped on every create and destroy. Public because Ref checks it; not
    // simulation state, so it is deliberately absent from the hash.
    std::uint64_t epoch() const { return epoch_; }

    // The next id this pool will stamp. Simulation state: it decides the
    // identity, and therefore the canonical serialization order, of every
    // entity created from here on, so it belongs in the hash.
    std::uint32_t nextSequence() const { return nextSequence_; }
    void setNextSequence(std::uint32_t value) { nextSequence_ = value; }

private:
    struct Slot {
        std::uint32_t generation = 1;  // never 0, so a live handle is never 0
        std::uint32_t dense = 0;
        std::uint32_t nextFree = kNoSlot;
        bool live = false;
    };

    static constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;

    std::uint32_t acquireSlot() {
        if (freeHead_ != kNoSlot) {
            const std::uint32_t index = freeHead_;
            freeHead_ = slots_[index].nextFree;
            if (freeHead_ == kNoSlot) {
                freeTail_ = kNoSlot;
            }
            slots_[index].nextFree = kNoSlot;
            return index;
        }
        assert(slots_.size() <= Handle<Tag>::kIndexMask && "pool exceeded 2^20 slots");
        slots_.push_back(Slot{});
        return static_cast<std::uint32_t>(slots_.size() - 1);
    }

    void releaseSlot(std::uint32_t index) {
        slots_[index].nextFree = kNoSlot;
        if (freeTail_ == kNoSlot) {
            freeHead_ = index;
        } else {
            slots_[freeTail_].nextFree = index;
        }
        freeTail_ = index;
    }

    const Slot* liveSlot(Id handle) const {
        if (!handle.valid() || handle.index() >= slots_.size()) {
            return nullptr;
        }
        const Slot& slot = slots_[handle.index()];
        if (!slot.live || slot.generation != handle.generation()) {
            return nullptr;
        }
        return &slot;
    }

    Slot* liveSlot(Id handle) {
        return const_cast<Slot*>(static_cast<const Pool*>(this)->liveSlot(handle));
    }

    std::vector<T> dense_;
    std::vector<std::uint32_t> denseToSlot_;
    std::vector<Slot> slots_;
    std::uint32_t freeHead_ = kNoSlot;
    std::uint32_t freeTail_ = kNoSlot;
    std::uint32_t nextSequence_ = 1;
    std::uint64_t epoch_ = 0;
};

}  // namespace ktd
