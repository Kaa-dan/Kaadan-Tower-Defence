#pragma once

// Deterministic random numbers for the simulation.
//
// <random> is banned in game/ (the guard test enforces it): the standard
// distributions are not specified bit-for-bit, so std::uniform_int_distribution
// gives different answers on libstdc++, libc++ and MSVC from the same engine
// and the same seed. Replays are a shipping feature, so the generator has to be
// ours and it has to be written out.

#include <array>
#include <cstddef>
#include <cstdint>

namespace ktd {

// One stream per consumer that draws for a distinct purpose.
//
// The problem this solves: with a single shared stream, adding one new draw
// anywhere shifts every subsequent draw in the whole simulation, so every
// recorded replay desyncs. Per-consumer streams mean adding a consumer only
// affects that consumer.
//
// Be honest about the limit -- substreams protect against *adding* a consumer.
// Changing the number or order of draws *within* a stream still invalidates old
// replays. Phase 7's simVersion is what catches that.
//
// APPEND ONLY. Never renumber, never reuse a retired value: the numeric value
// is baked into the seed, so renumbering silently rewrites the sequence.
enum class RngStream : std::uint32_t {
    WaveComposition = 1,
    SpawnJitter = 2,
    Targeting = 3,
    Crit = 4,
    Loot = 5,
    StatusProc = 6,
    Cosmetic = 7,  // never allowed to influence simulation state
};

// Sized to hold indices 0..kRngStreamCount-1, indexed by the raw enum value, so
// slot 0 is deliberately unused. Bump this when appending a stream.
inline constexpr std::size_t kRngStreamCount = 8;

// splitmix64. Used to derive well-separated states from adjacent seeds -- the
// seeds 1, 2 and 3 differ in one bit, and a raw xorshift state that close
// produces visibly correlated output for the first several draws.
inline constexpr std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) : state_(mix(seed)) {}

    std::uint32_t next() {
        // xorshift64*
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return static_cast<std::uint32_t>((state_ * 0x2545F4914F6CDD1Dull) >> 32);
    }

    // [0, 1), and the half-open bound is real rather than aspirational.
    //
    // The obvious `float(next()) / 2^32` does NOT satisfy it. uint32 to float
    // is not exact above 2^24 -- floats up there are spaced 256 apart -- so the
    // 128 largest results round up to exactly 2^32 and the division returns
    // 1.0f. About three draws in a hundred million: often enough to reach a
    // player, never often enough to show up in a test that was not looking.
    // range(lo, hi) could return hi, and the natural int(unit() * n) idiom
    // would index one past the end of a table.
    //
    // Taking the top 24 bits instead is exact -- 24 bits is all a float holds
    // anyway -- and the multiply by 2^-24 is exact, so the result is provably
    // below 1.
    float unit() { return static_cast<float>(next() >> 8) * 0x1p-24f; }

    float range(float lo, float hi) { return lo + unit() * (hi - lo); }

    // Uniform over [lo, hiInclusive]. Lemire's method: the naive `next() % span`
    // is biased whenever span does not divide 2^32, which for a d20 means some
    // faces come up measurably more often than others. Cheap to avoid, and
    // impossible to notice later without looking for it.
    int rangeInt(int lo, int hiInclusive) {
        // Every path consumes exactly one draw's worth of stream position,
        // degenerate ranges included. A draw count that varies with a branch is
        // the classic replay desync: a tick where the candidate list happened to
        // hold one element would leave the stream somewhere different from a
        // tick where it held two, and the divergence would surface later, in an
        // unrelated draw, with nothing pointing back here.
        const std::uint32_t raw = next();
        if (hiInclusive <= lo) {
            return lo;
        }

        const std::int64_t span64 = static_cast<std::int64_t>(hiInclusive) - lo + 1;
        // The full int range is 2^32, which truncates to 0 in a uint32 and would
        // make bounded() return 0 every time -- rangeInt(INT_MIN, INT_MAX) would
        // always answer INT_MIN. In that one case the raw draw already is the
        // uniform answer.
        if (span64 > 0xFFFFFFFFLL) {
            return static_cast<int>(static_cast<std::int64_t>(lo) + raw);
        }

        const std::uint32_t span = static_cast<std::uint32_t>(span64);
        return static_cast<int>(static_cast<std::int64_t>(lo) + bounded(raw, span));
    }

    // Exposed so the world hash covers the generator. A run that reaches the
    // same visible state through a different number of draws is NOT the same
    // run -- it diverges on the next draw -- and a hash that skipped this would
    // call the two identical.
    std::uint64_t state() const { return state_; }

private:
    static std::uint64_t mix(std::uint64_t seed) {
        const std::uint64_t mixed = splitmix64(seed);
        // xorshift64* has one fixed point: state 0 stays 0 forever. splitmix64
        // maps some input to it, so it has to be excluded.
        return mixed != 0 ? mixed : 0x9E3779B97F4A7C15ull;
    }

    // Uniform over [0, span) for span > 0, consuming the caller's first draw.
    std::uint32_t bounded(std::uint32_t first, std::uint32_t span) {
        std::uint64_t product = static_cast<std::uint64_t>(first) * span;
        std::uint32_t low = static_cast<std::uint32_t>(product);
        if (low < span) {
            // 2^32 % span, computed without a 64-bit divide.
            const std::uint32_t threshold = (0u - span) % span;
            while (low < threshold) {
                product = static_cast<std::uint64_t>(next()) * span;
                low = static_cast<std::uint32_t>(product);
            }
        }
        return static_cast<std::uint32_t>(product >> 32);
    }

    std::uint64_t state_;
};

// The simulation's set of streams, all derived from one world seed.
class RngStreams {
public:
    explicit RngStreams(std::uint64_t seed) {
        for (std::size_t i = 0; i < kRngStreamCount; ++i) {
            // Mixed multiplicatively into an already-mixed seed. Adding the
            // shifted index instead would make world seed S stream i identical
            // to world seed S + 2^32 stream i-1 -- not a determinism defect,
            // but a needless overlap between two players' games.
            streams_[i] = Rng(splitmix64(splitmix64(seed) ^
                                         (static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ull)));
        }
    }

    Rng& operator[](RngStream stream) { return streams_[static_cast<std::size_t>(stream)]; }
    const Rng& operator[](RngStream stream) const {
        return streams_[static_cast<std::size_t>(stream)];
    }

    std::size_t size() const { return streams_.size(); }
    const Rng& at(std::size_t index) const { return streams_[index]; }

private:
    // Default-constructed then overwritten in the ctor body; brace-init here
    // would be an aggregate init with omitted initialisers, which -Wextra
    // rightly complains about.
    std::array<Rng, kRngStreamCount> streams_;
};

}  // namespace ktd
