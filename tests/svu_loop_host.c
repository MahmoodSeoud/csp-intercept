/*
 * svu_loop_host.c - end-to-end proof of the Self-Verifying Uploader's resume loop.
 *
 * This exercises the WHOLE loop, not just the tracker: a sender that owns the
 * source + manifest, a channel that drops (Gilbert-Elliott burst loss, lib/ci_ge)
 * and corrupts packets, and a receiver (lib/ci_svu) that verifies and re-requests
 * exactly the bad byte ranges until the file is block-verified. Pure host code, no
 * libcsp, deterministic per seed.
 *
 * Scenario A (scripted): one packet is corrupted on its first delivery, clean on
 * resend. Proves the differential AND the repair -- round 1 has full coverage (the
 * old byte-counter would declare "delivered" on a corrupt file), SVU says CORRUPT
 * and names the block, round 2 re-sends just that block and the transfer verifies.
 *
 * Scenario B (realistic): a burst-loss + corrupting channel across a loss sweep.
 * Proves the loop always converges to COMPLETE_VERIFIED with bytes == source, and
 * reports rounds-to-converge and total transmission attempts (a goodput proxy).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ci_svu.h"
#include "ci_ge.h"
#include "ci_prng.h"

static int fails = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) { printf("FAIL: %s\n", (msg)); fails++; }  \
        else         { printf("ok:   %s\n", (msg)); }           \
    } while (0)

#define MAX_PAY 1024u

typedef struct {
    int use_ge;
    ci_ge_state_t ge;
    uint64_t cseed;         /* corruption draw seed                         */
    uint32_t corrupt_pct;   /* random corruption probability, percent       */
    uint32_t script_byte;   /* corrupt first delivery covering this byte     */
    int script_done;        /* ...only once                                  */
    uint32_t attempts;      /* total transmission attempts (all rounds)      */
} chan_t;

/* Transmit one payload fragment at `off` through the channel to `recv`. */
static void chan_send(chan_t *c, ci_svu_t *recv, uint32_t off,
                      const uint8_t *payload, uint32_t len)
{
    uint32_t a = c->attempts++;

    if (c->use_ge && ci_ge_state_step(&c->ge)) {
        return; /* dropped */
    }

    int corrupt = 0;
    if (c->corrupt_pct != 0u && (ci_draw(c->cseed, a) % 100u) < c->corrupt_pct) {
        corrupt = 1;
    }
    if (c->script_byte != UINT32_MAX && !c->script_done &&
        off <= c->script_byte && c->script_byte < off + len) {
        corrupt = 1;
        c->script_done = 1;
    }

    if (!corrupt) {
        ci_svu_accept(recv, off, payload, len);
        return;
    }
    uint8_t tmp[MAX_PAY];
    memcpy(tmp, payload, len);
    tmp[len / 2u] = (uint8_t)(tmp[len / 2u] ^ 0xFFu); /* flip one byte */
    ci_svu_accept(recv, off, tmp, len);
}

/* Blast every payload interval in `reqs` as `pay`-sized fragments. */
static void blast(chan_t *c, ci_svu_t *recv, const uint8_t *src,
                  const ci_svu_interval_t *reqs, uint32_t nreq, uint32_t pay)
{
    for (uint32_t r = 0u; r < nreq; r++) {
        for (uint32_t off = reqs[r].start; off < reqs[r].end; off += pay) {
            uint32_t len = (off + pay <= reqs[r].end) ? pay : (reqs[r].end - off);
            chan_send(c, recv, off, src + off, len);
        }
    }
}

int main(void)
{
    const uint32_t PAY = 200u;

    /* ---------- Scenario A: scripted lie-then-repair ---------- */
    {
        const uint32_t TOTAL = 4096u, BLOCK = 1024u;
        uint8_t *src = malloc(TOTAL);
        for (uint32_t i = 0u; i < TOTAL; i++) {
            src[i] = (uint8_t)((i * 31u) + 7u);
        }
        uint32_t nblk = ci_svu_nblocks(TOTAL, BLOCK);
        uint8_t *man = malloc((size_t)nblk * CI_SVU_HASH_LEN);
        ci_svu_manifest(src, TOTAL, BLOCK, man);

        ci_svu_t *recv = ci_svu_new(TOTAL, BLOCK, man, nblk);
        chan_t c = {0};
        c.script_byte = 2500u; /* lands in block 2 [2048,3072) */

        ci_svu_interval_t iv[64];
        uint32_t nout = 0u;
        ci_svu_interval_t whole = {0u, TOTAL};

        /* Round 1: send everything; the scripted packet gets corrupted. */
        blast(&c, recv, src, &whole, 1u, PAY);
        ci_svu_status_t st = ci_svu_verify(recv, iv, 64u, &nout);
        CHECK(ci_svu_covered(recv) == TOTAL,
              "A r1: full coverage -> OLD byte-counter would say DELIVERED");
        CHECK(st == CI_SVU_CORRUPT, "A r1: SVU says CORRUPT (the lie is caught)");
        CHECK(nout == 1u && iv[0].start == 2048u && iv[0].end == 3072u,
              "A r1: names block 2 [2048,3072) to re-request");

        /* Round 2: re-request exactly that range; clean this time. */
        blast(&c, recv, src, iv, nout, PAY);
        st = ci_svu_verify(recv, iv, 64u, &nout);
        CHECK(st == CI_SVU_COMPLETE_VERIFIED, "A r2: COMPLETE_VERIFIED after repair");
        CHECK(ci_svu_data(recv) != NULL &&
              memcmp(ci_svu_data(recv), src, TOTAL) == 0,
              "A r2: reassembled bytes == source (verified means correct)");

        ci_svu_free(recv);
        free(src);
        free(man);
    }

    /* ---------- Scenario B: realistic burst-loss + corruption sweep ---------- */
    {
        const uint32_t TOTAL = 65536u, BLOCK = 4096u;
        const uint64_t SEED = 0x5645524946ull; /* "VERIF" */
        const uint32_t MAX_ROUNDS = 500u;
        const double MEAN_BURST = 3.0;

        uint8_t *src = malloc(TOTAL);
        for (uint32_t i = 0u; i < TOTAL; i++) {
            src[i] = (uint8_t)(ci_draw(SEED, i) & 0xFFu);
        }
        uint32_t nblk = ci_svu_nblocks(TOTAL, BLOCK);
        uint8_t *man = malloc((size_t)nblk * CI_SVU_HASH_LEN);
        ci_svu_manifest(src, TOTAL, BLOCK, man);

        uint32_t cap = (TOTAL / PAY) + nblk + 16u;
        ci_svu_interval_t *req = malloc((size_t)cap * sizeof(*req));
        ci_svu_interval_t *ivs = malloc((size_t)cap * sizeof(*ivs));

        const double losses[3] = {0.05, 0.15, 0.30};
        printf("\n  loss   rounds   attempts   verdict\n");
        printf("  -----  ------   --------   -------\n");

        int all_ok = 1;
        for (int li = 0; li < 3; li++) {
            double p_g2b = 0.0, p_b2g = 0.0;
            ci_ge_params(losses[li], MEAN_BURST, &p_g2b, &p_b2g);

            chan_t c = {0};
            c.use_ge = 1;
            ci_ge_state_init(&c.ge, SEED, p_g2b, p_b2g);
            c.cseed = SEED ^ 0xC0FFEEull;
            c.corrupt_pct = 2u;
            c.script_byte = UINT32_MAX;

            ci_svu_t *recv = ci_svu_new(TOTAL, BLOCK, man, nblk);
            req[0].start = 0u;
            req[0].end = TOTAL;
            uint32_t nreq = 1u;

            uint32_t rounds = 0u;
            ci_svu_status_t st = CI_SVU_INCOMPLETE;
            uint32_t nout = 0u;
            while (rounds < MAX_ROUNDS) {
                rounds++;
                blast(&c, recv, src, req, nreq, PAY);
                st = ci_svu_verify(recv, ivs, cap, &nout);
                if (st == CI_SVU_COMPLETE_VERIFIED) {
                    break;
                }
                uint32_t n = (nout < cap) ? nout : cap;
                memcpy(req, ivs, (size_t)n * sizeof(*req));
                nreq = n;
            }

            int good = (st == CI_SVU_COMPLETE_VERIFIED) &&
                       (memcmp(ci_svu_data(recv), src, TOTAL) == 0);
            all_ok = all_ok && good;
            printf("  %4.0f%%  %6u   %8u   %s\n", losses[li] * 100.0, rounds,
                   c.attempts, good ? "VERIFIED == source" : "FAILED");
            ci_svu_free(recv);
        }
        printf("\n");
        CHECK(all_ok, "B: loop converges to VERIFIED==source at every loss level");

        free(src);
        free(man);
        free(req);
        free(ivs);
    }

    if (fails != 0) {
        printf("\n%d CHECK(s) FAILED\n", fails);
        return 1;
    }
    printf("all svu-loop checks passed\n");
    return 0;
}
