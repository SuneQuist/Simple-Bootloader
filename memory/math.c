#include <stdint.h>
#include "math.h"

double ceil(double x)
{
    /*
     * x86-64 SSE2 implementation.
     * Handles finite values within the int64 range.
     */
    long long whole = (long long)x;
    double result = (double)whole;

    if (result < x)
        result += 1.0;

    return result;
}

/*
 * Truncate a double toward zero.
 *
 * Lexbor's CSS selector engine uses trunc() when
 * evaluating nth-child and related selectors.
 *
 * Tiny64 is freestanding, so we provide this
 * function without depending on libm.
 */
double trunc(double x)
{
    /*
     * IEEE-754 double precision:
     *
     * 1 sign bit
     * 11 exponent bits
     * 52 fraction bits
     */
    union {
        double d;
        uint64_t u;
    } value;

    value.d = x;

    uint64_t exponent = (value.u >> 52) & 0x7FF;

    /* NaN or infinity: return unchanged. */
    if (exponent == 0x7FF)
        return x;

    int e = (int)exponent - 1023;

    /* Absolute value below 1: return signed zero. */
    if (e < 0) {
        value.u &= 0x8000000000000000ULL;
        return value.d;
    }

    /* All representable values are integral here. */
    if (e >= 52)
        return x;

    /* Clear the fractional bits. */
    uint64_t mask = (1ULL << (52 - e)) - 1ULL;

    value.u &= ~mask;

    return value.d;
}
