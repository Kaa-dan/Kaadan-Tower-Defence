#include "game/Math.h"

#include "TestFramework.h"

#include <cmath>
#include <limits>

using namespace ktd;

// powi replaces std::pow in the wave scaling. Being deterministic is not
// enough -- a function that returns 0 everywhere is perfectly deterministic --
// so this pins it to std::pow for the exponents the simulation actually uses.
//
// The tolerance is relative and deliberately loose enough to allow the
// last-bit disagreement that a different multiplication order produces, and
// tight enough to catch a genuinely wrong answer.
TEST(math_powi_matches_pow_for_small_exponents) {
    const float bases[] = {1.0f, 1.02f, 1.18f, 0.5f, 2.0f, 3.5f};
    for (const float base : bases) {
        for (int exp = 0; exp <= 30; ++exp) {
            const double expected = std::pow(static_cast<double>(base), static_cast<double>(exp));
            const double actual = static_cast<double>(math::powi(base, exp));
            CHECK_NEAR(actual, expected, expected * 1e-5);
        }
    }
}

// These are exact powers of two, so they must match bit for bit. CHECK_NEAR
// with a zero tolerance is the exact comparison -- CHECK_EQ would expand to
// `==`, which -Wfloat-equal rejects, and rightly so everywhere except here.
TEST(math_powi_handles_zero_and_negative_exponents) {
    CHECK_NEAR(math::powi(7.5f, 0), 1.0f, 0.0);
    CHECK_NEAR(math::powi(2.0f, 1), 2.0f, 0.0);
    CHECK_NEAR(math::powi(2.0f, 10), 1024.0f, 0.0);
    CHECK_NEAR(math::powi(2.0f, -3), 0.125f, 0.0);
    // INT_MIN cannot be negated; the guard must not overflow.
    CHECK_NEAR(math::powi(2.0f, (std::numeric_limits<int>::min)()), 0.0f, 0.0);
}

// Casting an out-of-range float to int is UB, and what the hardware returns
// differs between x86 and ARM -- which is a cross-platform desync hiding in a
// cast. floorToInt saturates instead, so the behaviour is identical everywhere.
TEST(math_floortoint_saturates_instead_of_invoking_ub) {
    CHECK_EQ(math::floorToInt(3.7f), 3);
    CHECK_EQ(math::floorToInt(-0.2f), -1);
    CHECK_EQ(math::floorToInt(-3.7f), -4);
    CHECK_EQ(math::floorToInt(0.0f), 0);

    CHECK_EQ(math::floorToInt(1e30f), (std::numeric_limits<int>::max)());
    CHECK_EQ(math::floorToInt(-1e30f), (std::numeric_limits<int>::min)());
    CHECK_EQ(math::floorToInt(std::numeric_limits<float>::infinity()),
             (std::numeric_limits<int>::max)());
    CHECK_EQ(math::floorToInt(-std::numeric_limits<float>::infinity()),
             (std::numeric_limits<int>::min)());
    CHECK_EQ(math::floorToInt(std::numeric_limits<float>::quiet_NaN()), 0);
}

TEST(math_clamp_and_abs_behave) {
    CHECK_EQ(math::clamp(5, 0, 3), 3);
    CHECK_EQ(math::clamp(-5, 0, 3), 0);
    CHECK_EQ(math::clamp(2, 0, 3), 2);
    CHECK_NEAR(math::min(2.0f, 3.0f), 2.0f, 0.0);
    CHECK_NEAR(math::max(2.0f, 3.0f), 3.0f, 0.0);
    CHECK_EQ(math::abs(-4), 4);
    CHECK_NEAR(math::abs(-4.5f), 4.5f, 0.0);
    // std::fabs, not a ternary: -0.0f must come back as +0.0f, because the two
    // have different bit patterns and the world hash is taken over bits.
    CHECK(!std::signbit(math::abs(-0.0f)));
}
