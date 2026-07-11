/*
 * ci_svu_test.c - proves the Self-Verifying Uploader core catches exactly the
 * failure libdtp's byte-counter misses. Three cases, all fed through DTP-style
 * offset-addressed fragments:
 *   1. clean full delivery      -> COMPLETE_VERIFIED
 *   2. a dropped high fragment  -> INCOMPLETE, exact missing byte range
 *   3. SILENT CORRUPTION        -> full coverage (old counter says "delivered")
 *                                  but SVU says CORRUPT and pinpoints the block
 * Case 3 is the whole thesis: same coverage the old receiver would accept, a
 * different verdict because we trust the bytes, not the counter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ci_svu.h"

static int fails = 0;

#define CHECK(cond, msg)                                     \
    do {                                                     \
        if (!(cond)) { printf("FAIL: %s\n", (msg)); fails++; } \
        else         { printf("ok:   %s\n", (msg)); }        \
    } while (0)

int main(void)
{
    const uint32_t TOTAL = 4096u;  /* payload size            */
    const uint32_t BLOCK = 1024u;  /* -> 4 integrity blocks   */
    const uint32_t PAY = 200u;     /* DTP effective payload per packet */

    uint8_t *src = malloc(TOTAL);
    for (uint32_t i = 0u; i < TOTAL; i++) {
        src[i] = (uint8_t)((i * 31u) + 7u);
    }

    uint32_t nblk = ci_svu_nblocks(TOTAL, BLOCK);
    uint8_t *manifest = malloc((size_t)nblk * CI_SVU_HASH_LEN);
    uint32_t got = ci_svu_manifest(src, TOTAL, BLOCK, manifest);
    CHECK(got == nblk && nblk == 4u, "manifest: 4 block digests");

    ci_svu_interval_t iv[16];
    uint32_t nout = 0u;
    ci_svu_status_t st;

    /* --- Case 1: clean full delivery --- */
    ci_svu_t *s = ci_svu_new(TOTAL, BLOCK, manifest, nblk);
    CHECK(s != NULL, "case1: receiver created");
    for (uint32_t off = 0u; off < TOTAL; off += PAY) {
        uint32_t len = (off + PAY <= TOTAL) ? PAY : (TOTAL - off);
        ci_svu_accept(s, off, src + off, len);
    }
    st = ci_svu_verify(s, iv, 16u, &nout);
    CHECK(ci_svu_covered(s) == TOTAL, "case1: full coverage");
    CHECK(st == CI_SVU_COMPLETE_VERIFIED, "case1: COMPLETE_VERIFIED");
    CHECK(nout == 0u, "case1: nothing to re-request");
    ci_svu_free(s);

    /* --- Case 2: a dropped high fragment (loss) --- */
    s = ci_svu_new(TOTAL, BLOCK, manifest, nblk);
    uint32_t drop_off = 3000u; /* a real fragment offset (multiple of PAY) */
    for (uint32_t off = 0u; off < TOTAL; off += PAY) {
        if (off == drop_off) {
            continue;
        }
        uint32_t len = (off + PAY <= TOTAL) ? PAY : (TOTAL - off);
        ci_svu_accept(s, off, src + off, len);
    }
    st = ci_svu_verify(s, iv, 16u, &nout);
    CHECK(ci_svu_covered(s) == TOTAL - PAY, "case2: old counter sees short (< total)");
    CHECK(st == CI_SVU_INCOMPLETE, "case2: INCOMPLETE");
    CHECK(nout == 1u && iv[0].start == drop_off && iv[0].end == drop_off + PAY,
          "case2: exact missing byte range returned");
    ci_svu_free(s);

    /* --- Case 3: SILENT CORRUPTION (the differential) --- */
    s = ci_svu_new(TOTAL, BLOCK, manifest, nblk);
    uint8_t *corrupt = malloc(TOTAL);
    memcpy(corrupt, src, TOTAL);
    corrupt[2500] = (uint8_t)(corrupt[2500] ^ 0xFFu); /* flip a byte in block 2 */
    for (uint32_t off = 0u; off < TOTAL; off += PAY) {
        uint32_t len = (off + PAY <= TOTAL) ? PAY : (TOTAL - off);
        ci_svu_accept(s, off, corrupt + off, len);
    }
    st = ci_svu_verify(s, iv, 16u, &nout);
    CHECK(ci_svu_covered(s) == TOTAL,
          "case3: OLD byte-counter says DELIVERED (covered == total)");
    CHECK(st == CI_SVU_CORRUPT, "case3: SVU says CORRUPT");
    CHECK(nout == 1u && iv[0].start == 2048u && iv[0].end == 3072u,
          "case3: pinpoints block 2 [2048,3072) to re-request");
    ci_svu_free(s);

    free(src);
    free(corrupt);
    free(manifest);

    if (fails != 0) {
        printf("\n%d CHECK(s) FAILED\n", fails);
        return 1;
    }
    printf("\nall ci_svu checks passed\n");
    return 0;
}
