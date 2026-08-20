#include "game/Math.h"

namespace ktd::math {

float powi(float base, int exp) {
    if (exp < 0) {
        // Guard the one value that cannot be negated: -INT_MIN overflows.
        if (exp == (std::numeric_limits<int>::min)()) {
            return 0.0f;
        }
        return 1.0f / powi(base, -exp);
    }

    float result = 1.0f;
    float factor = base;
    int remaining = exp;
    while (remaining > 0) {
        if ((remaining & 1) != 0) {
            result *= factor;
        }
        remaining >>= 1;
        // The final squaring is dead work, but skipping it would need a branch
        // that also changes the multiplication order. Leave it: correctness of
        // the rounding order is worth more here than one multiply per wave.
        factor *= factor;
    }
    return result;
}

}  // namespace ktd::math
