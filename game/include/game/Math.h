#pragma once

// The single audited math surface for the simulation.
//
// This is the ONLY file in game/ permitted to include <cmath>, and the
// purity_determinism guard enforces that. The reason is ADR 0002: IEEE-754
// + - * / and sqrt are correctly rounded and bit-identical everywhere, but the
// transcendental libm functions (pow, sin, exp, atan2) are not, and differ
// between glibc, MSVC and Apple libm. Routing every call through here means the
// list of things that can diverge stays short enough to audit by reading one
// file.
//
// Anything added here must be either an IEEE-exact primitive or an algorithm
// written out of such primitives. If it forwards to libm, it does not belong.

#include <cfloat>
#include <cmath>
#include <limits>

namespace ktd::math {

// The float-with-discipline strategy assumes IEEE-754 binary32 and no excess
// intermediate precision. Both are true on every target this ships to
// (x86-64 with SSE2, ARM64), but neither is guaranteed by the language, so we
// fail at compile time rather than desync at runtime on some future target.
static_assert(
    std::numeric_limits<float>::is_iec559,
    "kaadanTD requires IEEE-754 floats; see docs/decisions/0002-float-not-fixed-point.md");
static_assert(FLT_EVAL_METHOD == 0,
              "float arithmetic must evaluate at float precision; excess intermediate precision "
              "(x87) breaks cross-platform reproducibility");

// --- IEEE-exact primitives ----------------------------------------------
// Correctly rounded by the standard and compiled to a single instruction on
// every target, so these are safe to call directly.

inline float sqrt(float v) { return std::sqrt(v); }
inline float floor(float v) { return std::floor(v); }
inline float abs(float v) { return std::fabs(v); }

// --- Comparisons ---------------------------------------------------------

template <typename T>
constexpr T min(T a, T b) {
    return b < a ? b : a;
}

template <typename T>
constexpr T max(T a, T b) {
    return a < b ? b : a;
}

template <typename T>
constexpr T clamp(T v, T lo, T hi) {
    return min(max(v, lo), hi);
}

constexpr int abs(int v) { return v < 0 ? -v : v; }

// --- Conversions ---------------------------------------------------------

// Converting an out-of-range float to int is undefined behaviour, and the
// result the hardware happens to produce differs by architecture (x86 yields
// INT_MIN, ARM64 saturates). Clamping first makes the conversion always
// defined, which turns a silent cross-platform divergence into a boring
// saturation. NaN maps to 0 rather than to whatever falls out of the cast.
inline int floorToInt(float v) {
    if (std::isnan(v)) {
        return 0;
    }
    const float f = floor(v);
    // Both bounds are exactly representable as float, so these comparisons are
    // exact and the surviving value is guaranteed to fit in an int.
    if (f <= -2147483648.0f) {
        return (std::numeric_limits<int>::min)();
    }
    if (f >= 2147483648.0f) {
        return (std::numeric_limits<int>::max)();
    }
    return static_cast<int>(f);
}

// --- Algorithms built from exact primitives ------------------------------

// Integer exponentiation by squaring. This exists to replace std::pow, which is
// the one confirmed cross-platform divergence in the simulation. It is built
// only from multiplication and division, so it is bit-identical everywhere.
//
// It does NOT agree with std::pow to the last bit — a different multiplication
// order rounds differently. That is expected and fine; what matters is that it
// rounds the same way on every machine.
float powi(float base, int exp);

}  // namespace ktd::math
