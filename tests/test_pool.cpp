#include "TestFramework.h"

#include "game/Pool.h"

#include <vector>

using namespace ktd;

namespace {

struct Thing {
    std::uint32_t id = 0;  // stamped by the pool
    int payload = 0;
};

struct ThingTag {};
using ThingPool = Pool<Thing, ThingTag>;
using ThingId = ThingPool::Id;

}  // namespace

// The reason the pool exists. The old code handed out raw pointers into a
// std::vector; the first tower that killed two enemies in one step would have
// left one of them dangling. A handle to a destroyed entity has to fail, and
// fail by saying so rather than by returning memory that now holds someone else.
TEST(pool_stale_handle_returns_null) {
    ThingPool pool;
    const ThingId a = pool.create({0, 10});
    const ThingId b = pool.create({0, 20});

    CHECK(pool.get(a) != nullptr);
    CHECK_EQ(pool.get(a)->payload, 10);

    CHECK(pool.destroy(a));
    CHECK(pool.get(a) == nullptr);
    CHECK(!pool.alive(a));

    // Destroying twice must be a no-op, not a double free. Two projectiles
    // landing on the same enemy in one tick do exactly this, and the gold payout
    // depends on the second call reporting that it did nothing.
    CHECK(!pool.destroy(a));

    // The survivor was moved by the swap-and-pop and must still be reachable
    // through its own handle -- that is the whole point of the indirection.
    CHECK(pool.get(b) != nullptr);
    CHECK_EQ(pool.get(b)->payload, 20);

    // A recycled slot must not answer to the old handle. This is the case a
    // plain index would get wrong: `a`'s slot is reused below, and an index-only
    // handle would happily resolve to the new occupant.
    const ThingId c = pool.create({0, 30});
    CHECK_EQ(c.index(), a.index());  // FIFO handed the slot back
    CHECK(c != a);                   // ...but not the same handle
    CHECK(pool.get(a) == nullptr);
    CHECK_EQ(pool.get(c)->payload, 30);

    // Two handles that were never issued must also fail rather than index
    // out of bounds.
    CHECK(pool.get(ThingId{}) == nullptr);
    CHECK(pool.get(ThingId::make(9999, 1)) == nullptr);
}

// 12 bits of generation is 4,095 reuses of one slot before the counter wraps.
// A long session reaches that, so the wrap has to be correct rather than
// merely unlikely -- and it has to be deterministic, or every replay recorded
// across a wrap desyncs.
TEST(pool_survives_generation_wrap) {
    ThingPool pool;

    // Drive one slot around its generation counter more than a full turn. The
    // pool is kept at a single element so the free list always returns the same
    // slot, which is the fastest way to a wrap and the case LIFO would hit in
    // real use.
    ThingId handle = pool.create({0, 0});
    const std::uint32_t slot = handle.index();
    std::vector<ThingId> seen;

    for (std::uint32_t i = 0; i < ThingId::kMaxGeneration + 100u; ++i) {
        seen.push_back(handle);
        CHECK(pool.destroy(handle));
        handle = pool.create({0, static_cast<int>(i)});
        CHECK_EQ(handle.index(), slot);
        // A wrapped generation must never produce the zero bit pattern, because
        // that is the invalid handle -- a live entity answering to a
        // default-constructed handle would be worse than a dangling pointer.
        CHECK(handle.valid());
        CHECK(handle.generation() != 0u);
    }

    // The live handle resolves; every retired one does not. After a wrap one
    // retired handle in 4,095 necessarily collides -- generations are finite, so
    // this is a delay, not a cure -- and the test pins that the collision is
    // exactly the wrap and nothing sooner.
    CHECK(pool.get(handle) != nullptr);
    int collisions = 0;
    for (const ThingId old : seen) {
        if (old.generation() == handle.generation()) {
            ++collisions;
        } else {
            CHECK(pool.get(old) == nullptr);
        }
    }
    CHECK_EQ(collisions, 1);

    // And the wrap is deterministic: an identical sequence of operations on a
    // second pool lands on the identical handle. This is what keeps a replay
    // valid across a wrap.
    ThingPool replay;
    ThingId mirror = replay.create({0, 0});
    for (std::uint32_t i = 0; i < ThingId::kMaxGeneration + 100u; ++i) {
        replay.destroy(mirror);
        mirror = replay.create({0, static_cast<int>(i)});
    }
    CHECK_EQ(mirror.bits, handle.bits);
}

// The pool stamps a monotonic id, and that id -- not the handle, not the array
// position -- is the entity's identity. Slots recycle; ids never do. Everything
// that has to stay stable across a save, a replay or a swap-and-pop keys on this.
TEST(pool_ids_are_monotonic_and_never_recycled) {
    ThingPool pool;
    std::vector<std::uint32_t> issued;

    for (int round = 0; round < 50; ++round) {
        const ThingId h = pool.create({0, round});
        issued.push_back(pool.get(h)->id);
        pool.destroy(h);  // frees the slot, but not the id
    }

    for (std::size_t i = 1; i < issued.size(); ++i) {
        CHECK(issued[i] > issued[i - 1]);
    }
    CHECK_EQ(issued.front(), std::uint32_t(1));  // 0 is the invalid id
}

// Swap-and-pop moves an arbitrary survivor into the hole. Every handle the pool
// ever issued and has not retired must still resolve to the entity it named --
// otherwise removal silently renames live entities, which is precisely the bug
// class that made array position unusable as an identity.
TEST(pool_handles_survive_arbitrary_removal) {
    ThingPool pool;
    std::vector<ThingId> handles;
    for (int i = 0; i < 64; ++i) {
        handles.push_back(pool.create({0, i}));
    }

    // Remove every third, front to back: enough churn to move most of the array.
    std::vector<ThingId> removed;
    for (std::size_t i = 0; i < handles.size(); i += 3) {
        CHECK(pool.destroy(handles[i]));
        removed.push_back(handles[i]);
    }

    for (std::size_t i = 0; i < handles.size(); ++i) {
        const Thing* thing = pool.get(handles[i]);
        if (i % 3 == 0) {
            CHECK(thing == nullptr);
        } else {
            CHECK(thing != nullptr);
            CHECK_EQ(thing->payload, static_cast<int>(i));  // still names its own entity
        }
    }
    CHECK_EQ(pool.size(), handles.size() - removed.size());

    // handleAt is the bridge from iteration order back to identity, and it must
    // agree with what the dense array holds at that position.
    for (std::size_t i = 0; i < pool.size(); ++i) {
        CHECK_EQ(pool.get(pool.handleAt(i))->id, pool.items()[i].id);
    }
}

// FIFO reuse is not a micro-optimisation; it is what makes the generation
// counter last. LIFO hands the same slot straight back, so a create/destroy pair
// in a loop burns one slot's 4,095 generations while the rest of the table sits
// untouched.
TEST(pool_free_list_is_fifo) {
    ThingPool pool;
    const ThingId a = pool.create({0, 1});
    const ThingId b = pool.create({0, 2});
    const ThingId c = pool.create({0, 3});

    pool.destroy(a);
    pool.destroy(b);
    pool.destroy(c);

    // Freed a, b, c -- so they come back in that order, not c, b, a.
    CHECK_EQ(pool.create({0, 4}).index(), a.index());
    CHECK_EQ(pool.create({0, 5}).index(), b.index());
    CHECK_EQ(pool.create({0, 6}).index(), c.index());
}
