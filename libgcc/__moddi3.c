/*
 * Signed 64-bit remainder helper for 32-bit targets.
 *
 * Clang lowers signed long long remainder on PowerPC32 to __moddi3.  Keep the
 * implementation in terms of the unsigned division primitive so compiling
 * this helper cannot recurse back into __moddi3.
 */

#include "libgcc.h"

int64_t __moddi3(int64_t num, int64_t den)
{
    uint64_t unum = (uint64_t)num;
    uint64_t uden = (uint64_t)den;
    uint64_t rem;
    int negative = num < 0;

    /* Convert to unsigned magnitudes without signed overflow at INT64_MIN. */
    if (num < 0)
        unum = 0 - unum;
    if (den < 0)
        uden = 0 - uden;

    (void)__udivmoddi4(unum, uden, &rem);
    return negative ? -(int64_t)rem : (int64_t)rem;
}
