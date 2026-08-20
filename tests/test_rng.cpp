#include "game/Rng.h"

#include "TestFramework.h"

#include <array>
#include <cstdint>
#include <vector>

using namespace ktd;

// The whole point of per-consumer streams: adding a draw to one stream must not
// shift what any other stream produces. If this fails, every future feature
// that consumes randomness silently invalidates every recorded replay.
TEST(rng_streams_are_independent) {
    RngStreams baseline(4242);
    std::vector<std::uint32_t> targetingBefore;
    for (int i = 0; i < 16; ++i) {
        targetingBefore.push_back(baseline[RngStream::Targeting].next());
    }

    // A fresh world with the same seed, where a new feature now draws from
    // SpawnJitter before Targeting is consulted.
    RngStreams withNewConsumer(4242);
    for (int i = 0; i < 100; ++i) {
        (void)withNewConsumer[RngStream::SpawnJitter].next();
    }
    for (int i = 0; i < 16; ++i) {
        CHECK_EQ(withNewConsumer[RngStream::Targeting].next(), targetingBefore[std::size_t(i)]);
    }
}

// Adjacent seeds used to produce visibly correlated runs, because the old
// constructor dropped the seed straight into the xorshift state and 1, 2 and 3
// differ by one bit. splitmix64 mixing is what fixes it.
TEST(rng_adjacent_seeds_are_decorrelated) {
    Rng a(1);
    Rng b(2);
    Rng c(3);
    int matches = 0;
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t x = a.next();
        const std::uint32_t y = b.next();
        const std::uint32_t z = c.next();
        // Compare high bytes: correlated states agree in the top bits long
        // before they agree in full.
        if ((x >> 24) == (y >> 24)) {
            ++matches;
        }
        if ((y >> 24) == (z >> 24)) {
            ++matches;
        }
    }
    // 128 comparisons of one byte each: chance alone gives about 0.5 hits.
    CHECK(matches < 8);
}

// Streams are derived from the world seed, so they must not collide with each
// other either -- two streams producing the same sequence would be one stream
// wearing two names.
TEST(rng_streams_do_not_alias_each_other) {
    RngStreams streams(99);
    std::array<std::uint32_t, kRngStreamCount> first{};
    for (std::size_t i = 0; i < kRngStreamCount; ++i) {
        Rng copy = streams.at(i);
        first[i] = copy.next();
    }
    for (std::size_t i = 0; i < kRngStreamCount; ++i) {
        for (std::size_t j = i + 1; j < kRngStreamCount; ++j) {
            CHECK(first[i] != first[j]);
        }
    }
}

// General uniformity over a small range. This is a regression guard, not proof
// that the rejection loop works: for a span of 6 the modulo bias is about one
// part in a billion and no sample size here would see it. The test below is the
// one that actually discriminates.
TEST(rng_rangeint_is_unbiased) {
    constexpr int kBuckets = 6;
    constexpr int kSamples = 600000;
    std::array<int, kBuckets> counts{};

    Rng rng(20260820);
    for (int i = 0; i < kSamples; ++i) {
        const int roll = rng.rangeInt(1, kBuckets);
        CHECK(roll >= 1 && roll <= kBuckets);
        counts[std::size_t(roll - 1)] += 1;
    }

    // Chi-squared against a uniform expectation. With 5 degrees of freedom the
    // 99.9th percentile is 20.5, so a fair generator clears this essentially
    // always and a biased one does not.
    const double expected = double(kSamples) / double(kBuckets);
    double chiSquared = 0.0;
    for (const int count : counts) {
        const double delta = double(count) - expected;
        chiSquared += (delta * delta) / expected;
    }
    CHECK(chiSquared < 20.5);
}

// This is the test that proves Lemire's rejection loop is running.
//
// Modulo bias is only visible when the span is a large fraction of 2^32. With a
// span of exactly 3 * 2^30, `next() % span` maps the top quarter of the
// generator's output back onto the bottom third of the range, so that third
// comes up twice as often as the other two -- 50% of draws instead of 33%.
// An unbiased implementation splits them evenly.
TEST(rng_rangeint_rejects_instead_of_folding_the_top_range) {
    constexpr std::int64_t kLo = -2147483648LL;    // INT_MIN
    constexpr std::int64_t kThird = 1073741824LL;  // 2^30
    constexpr int kSamples = 200000;

    Rng rng(31337);
    int inFirstThird = 0;
    for (int i = 0; i < kSamples; ++i) {
        const int roll = rng.rangeInt(int(kLo), int(kLo + 3 * kThird - 1));
        if (std::int64_t(roll) - kLo < kThird) {
            ++inFirstThird;
        }
    }

    const double fraction = double(inFirstThird) / double(kSamples);
    // Unbiased gives 0.333; the modulo version gives 0.500. The bound sits far
    // from both, so this fails loudly rather than flaking.
    CHECK(fraction > 0.31 && fraction < 0.36);
}

TEST(rng_rangeint_handles_degenerate_and_wide_ranges) {
    Rng rng(7);
    CHECK_EQ(rng.rangeInt(5, 5), 5);
    CHECK_EQ(rng.rangeInt(9, 2), 9);  // inverted range must not read past the end

    // A span wider than int32 must not overflow the intermediate arithmetic.
    for (int i = 0; i < 64; ++i) {
        const int roll = rng.rangeInt(-2000000000, 2000000000);
        CHECK(roll >= -2000000000 && roll <= 2000000000);
    }
}

// The generator's own position is part of the run. Two worlds that look
// identical but have consumed different numbers of draws will diverge on the
// next one, so state() has to be observable for the world hash to be honest.
TEST(rng_exposes_state_for_hashing) {
    Rng a(123);
    Rng b(123);
    CHECK_EQ(a.state(), b.state());
    (void)a.next();
    CHECK(a.state() != b.state());
}
