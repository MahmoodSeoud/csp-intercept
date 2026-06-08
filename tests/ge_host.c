/*
 * ge_host.c - unit test for the Gilbert-Elliott burst-loss generator (lib/ci_ge).
 *
 * Standalone, no hardware. Verifies: parameter derivation, marginal loss ~ target
 * across the sweep range, mean burst length ~ target, determinism (same seed ->
 * identical vector; different seed differs), the 0% control, and that the generated
 * vector drives ci_rule_decide()'s existing replay path (the whole point of T3:
 * Gilbert-Elliott reuses the reproducible replay path, no new hot path).
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ci_ge.h"
#include "ci_rule.h"

static double marginal_loss(const uint8_t *v, size_t n) {
    size_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d += v[i];
    }
    return (double)d / (double)n;
}

static double mean_burst(const uint8_t *v, size_t n) {
    size_t bursts = 0, drops = 0;
    int in = 0;
    for (size_t i = 0; i < n; i++) {
        if (v[i]) {
            drops++;
            if (!in) { bursts++; in = 1; }
        } else {
            in = 0;
        }
    }
    return bursts ? (double)drops / (double)bursts : 0.0;
}

int main(void) {
    double p, r;

    /* 1) param derivation: loss=0.1, burst=5 -> r=0.2, p=0.2*0.1/0.9 */
    ci_ge_params(0.1, 5.0, &p, &r);
    assert(fabs(r - 0.2) < 1e-9);
    assert(fabs(p - (0.2 * 0.1 / 0.9)) < 1e-9);

    const size_t N = 2000000;
    uint8_t *v = malloc(N), *v2 = malloc(N);
    assert(v && v2);

    /* 2) marginal loss tracks target across the sweep range (within 1 point over 2M) */
    double targets[] = {0.01, 0.05, 0.10, 0.20, 0.30};
    for (int i = 0; i < 5; i++) {
        ci_ge_params(targets[i], 5.0, &p, &r);
        ci_ge_fill(0xC0FFEEu, p, r, v, N);
        double L = marginal_loss(v, N);
        printf("  target=%.2f  measured=%.4f  mean_burst=%.2f\n",
               targets[i], L, mean_burst(v, N));
        assert(fabs(L - targets[i]) < 0.01);
    }

    /* 3) mean burst length tracks target */
    ci_ge_params(0.2, 8.0, &p, &r);
    ci_ge_fill(0x1234u, p, r, v, N);
    assert(fabs(mean_burst(v, N) - 8.0) < 1.0);

    /* 4) determinism: same seed -> identical; different seed -> differs */
    ci_ge_params(0.2, 5.0, &p, &r);
    ci_ge_fill(42, p, r, v, N);
    ci_ge_fill(42, p, r, v2, N);
    assert(memcmp(v, v2, N) == 0);
    ci_ge_fill(43, p, r, v2, N);
    assert(memcmp(v, v2, N) != 0);

    /* 5) 0% control -> no drops */
    ci_ge_params(0.0, 5.0, &p, &r);
    ci_ge_fill(7, p, r, v, N);
    assert(marginal_loss(v, N) == 0.0);

    /* 6) the generated vector drives ci_rule_decide()'s replay path */
    ci_ge_params(0.2, 5.0, &p, &r);
    ci_ge_fill(99, p, r, v, 1000);
    ci_drop_rule_t rule = {0};
    rule.seed = 99;
    rule.match_dport = -1;
    rule.replay_vector = v;
    rule.replay_len = 1000;
    for (size_t i = 0; i < 1000; i++) {
        assert(ci_rule_decide(&rule, i) == (v[i] ? 1 : 0));
    }
    assert(ci_rule_decide(&rule, 5000) == 0);   /* out of range -> keep */

    free(v);
    free(v2);
    printf("ge_host: PASS\n");
    return 0;
}
