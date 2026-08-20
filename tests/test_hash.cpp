#include "game/Hash.h"

#include "Scenario.h"
#include "TestFramework.h"

#include "game/World.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace ktd;

// The acceptance test for the whole phase.
//
// 3,000 ticks of a scripted run must produce exactly this checksum. The
// constant is committed, so any change to simulation behaviour -- a balance
// tweak, a reordered loop, an extra RNG draw -- fails it loudly instead of
// silently invalidating every replay recorded before the change.
//
// When it fails after an intentional change: confirm the change was intended,
// then update the constant in the same commit that made it, and say so in the
// message. Updating it separately hides exactly what this test exists to show.
TEST(determinism_golden) {
    constexpr std::uint64_t kGoldenChecksum = 0x6684E7DA901C0081ULL;

    World world(config::kGridWidth, config::kGridHeight, scenario::kSeed);
    scenario::run(world, [](std::uint64_t, const World&) {});

    CHECK_EQ(world.checksum(), kGoldenChecksum);
}

// A hash that never changes would pass the golden test forever while proving
// nothing. Every tick of the scripted run must be distinguishable.
TEST(checksum_changes_as_the_world_advances) {
    World world(config::kGridWidth, config::kGridHeight, scenario::kSeed);
    std::vector<std::uint64_t> seen;
    scenario::run(world, [&](std::uint64_t, const World& w) { seen.push_back(w.checksum()); });

    int distinct = 0;
    for (std::size_t i = 1; i < seen.size(); ++i) {
        if (seen[i] != seen[i - 1]) {
            ++distinct;
        }
    }
    // Most ticks move something. Not all -- between waves the field is empty
    // and only the tick counter advances, which does change the hash, so this
    // bound is comfortable rather than tight.
    CHECK(distinct > 2000);
}

// Two worlds from the same seed must agree bit for bit at every tick, not
// merely end up close. This replaces the old field-by-field comparison with a
// 1e-6 tolerance, which could not have detected a genuine divergence.
TEST(identical_seeds_produce_identical_checksums_at_every_tick) {
    World a(config::kGridWidth, config::kGridHeight, scenario::kSeed);
    World b(config::kGridWidth, config::kGridHeight, scenario::kSeed);

    std::vector<std::uint64_t> traceA;
    scenario::run(a, [&](std::uint64_t, const World& w) { traceA.push_back(w.checksum()); });

    std::size_t index = 0;
    scenario::run(b, [&](std::uint64_t tick, const World& w) {
        if (w.checksum() != traceA[index]) {
            testing::reportFailure(__FILE__, __LINE__, "diverged at tick " + std::to_string(tick));
        }
        ++index;
    });
}

// Different seeds must not collide into the same run -- otherwise the golden
// test would pass for a world that ignores its seed entirely.
TEST(different_seeds_produce_different_checksums) {
    World a(config::kGridWidth, config::kGridHeight, scenario::kSeed);
    World b(config::kGridWidth, config::kGridHeight, scenario::kSeed + 1);
    scenario::run(a, [](std::uint64_t, const World&) {});
    scenario::run(b, [](std::uint64_t, const World&) {});
    CHECK(a.checksum() != b.checksum());
}

// The hash covers a serialization, not struct memory. Padding bytes are
// uninitialised and not portable, so hashing raw bytes would make two identical
// runs disagree across compilers.
TEST(hasher_canonicalises_signed_zero_and_nan) {
    Hasher positive;
    Hasher negative;
    positive.f32(0.0f);
    negative.f32(-0.0f);
    CHECK_EQ(positive.value(), negative.value());

    Hasher nanA;
    Hasher nanB;
    nanA.f32(std::bit_cast<float>(std::uint32_t(0x7FC00001u)));
    nanB.f32(std::bit_cast<float>(std::uint32_t(0xFFC00007u)));
    CHECK_EQ(nanA.value(), nanB.value());

    // ...but ordinary distinct values must still hash apart.
    Hasher one;
    Hasher two;
    one.f32(1.0f);
    two.f32(1.0000001f);
    CHECK(one.value() != two.value());
}

// Byte order is written out explicitly rather than memcpy'd, so the hash is the
// same on a big-endian target. Nothing here is big-endian today; the test
// documents the intent and pins the byte order for whoever ports it.
TEST(hasher_writes_little_endian_regardless_of_host) {
    Hasher wide;
    wide.u32(0x01020304u);

    Hasher bytes;
    bytes.u8(0x04);
    bytes.u8(0x03);
    bytes.u8(0x02);
    bytes.u8(0x01);

    CHECK_EQ(wide.value(), bytes.value());
}

// The FMA-contraction canary, and usually the first thing to break.
//
// The trace in tests/data/reference_trace.tsv is the exact output of
// ktd_hashdump, committed. This test regenerates it in-process and diffs.
// Because CI runs the suite under both the core (Debug) and core-release
// presets, both legs comparing against the same committed file is what proves
// Debug and Release agree -- an in-process test cannot compare two builds
// itself, and pretending otherwise would be the kind of test that passes
// without checking anything.
//
// The same file is the artifact the three-OS CI job diffs, so a local failure
// here and a cross-platform failure there mean the same thing.
TEST(determinism_debug_matches_release) {
    std::ifstream reference(KTD_REFERENCE_TRACE);
    if (!reference) {
        testing::reportFailure(
            __FILE__, __LINE__,
            std::string("cannot open reference trace at ") + KTD_REFERENCE_TRACE);
        return;
    }

    std::vector<std::string> expected;
    std::string line;
    while (std::getline(reference, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // a Windows checkout may have rewritten the endings
        }
        if (!line.empty()) {
            expected.push_back(line);
        }
    }

    World world(config::kGridWidth, config::kGridHeight, scenario::kSeed);
    std::vector<std::string> actual;
    scenario::run(world, [&](std::uint64_t tick, const World& w) {
        std::ostringstream row;
        row << tick << '\t' << std::hex << std::setfill('0') << std::setw(16) << w.checksum();
        actual.push_back(row.str());
    });

    if (expected.size() != actual.size()) {
        testing::reportFailure(__FILE__, __LINE__,
                               "reference trace has " + std::to_string(expected.size()) +
                                   " ticks, this run produced " + std::to_string(actual.size()));
        return;
    }

    // Name the first divergent tick. "The traces differ" sends someone
    // bisecting by hand; "diverged at tick 1847" points at the bug.
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            testing::reportFailure(__FILE__, __LINE__,
                                   "first divergence at tick " + std::to_string(i + 1) +
                                       ": reference " + expected[i] + ", got " + actual[i]);
            return;
        }
    }
}
