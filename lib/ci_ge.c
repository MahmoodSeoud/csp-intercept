#include "ci_ge.h"
#include "ci_prng.h"

void ci_ge_params(double loss, double mean_burst, double *p_g2b, double *p_b2g) {
    double p, r;
    if (mean_burst < 1.0) {
        mean_burst = 1.0;
    }
    r = 1.0 / mean_burst;                 /* mean BAD run length = 1/r (geometric) */
    if (loss <= 0.0) {
        p = 0.0;                          /* never enter BAD */
    } else if (loss >= 1.0) {
        p = 1.0;                          /* always BAD */
        r = 0.0;
    } else {
        /* steady-state pi_bad = p/(p+r) = loss  ->  p = r*loss/(1-loss) */
        p = r * loss / (1.0 - loss);
        if (p > 1.0) {
            p = 1.0;
        }
    }
    if (p_g2b) {
        *p_g2b = p;
    }
    if (p_b2g) {
        *p_b2g = r;
    }
}

/* p in [0,1] -> a splitmix64 threshold, matching ci_rule_decide's convention:
 * draw < threshold means "event fires". p<=0 -> never; p>=1 -> always. */
static uint64_t ci_ge_threshold(double p) {
    if (p <= 0.0) {
        return 0;                         /* draw < 0 is never true */
    }
    if (p >= 1.0) {
        return UINT64_MAX;                /* draw < MAX is true except for the single
                                           * value MAX (prob 2^-64); negligible, and
                                           * p>=1 is outside the measured sweep range */
    }
    /* p*2^64 is exactly representable and < 2^64 for p in (0,1); see ci_rule_decide. */
    return (uint64_t)(p * 18446744073709551616.0);
}

void ci_ge_fill(uint64_t seed, double p_g2b, double p_b2g, uint8_t *vec, size_t n) {
    if (vec == NULL || n == 0) {
        return;
    }
    const uint64_t t_g2b = ci_ge_threshold(p_g2b);
    const uint64_t t_b2g = ci_ge_threshold(p_b2g);
    int bad = 0;                          /* chain starts in GOOD */
    for (size_t i = 0; i < n; i++) {
        vec[i] = bad ? 1u : 0u;           /* GOOD lossless, BAD drops */
        uint64_t d = ci_draw(seed, i);    /* one independent draw per index */
        if (!bad) {
            if (d < t_g2b) {
                bad = 1;
            }
        } else {
            if (d < t_b2g) {
                bad = 0;
            }
        }
    }
}
