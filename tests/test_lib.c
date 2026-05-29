/*
 * test_lib.c - unit tests for the csp-intercept shared lib.
 *
 * Dependency-free (no libcsp, no test framework): just <assert>-style checks so
 * it compiles and runs with a bare C compiler. Every test maps to a finding from
 * the engineering review (the gotchas that only source-reading caught).
 *
 * Run: meson test  (or: cc -I lib lib/ci_rdp.c lib/ci_dtp.c lib/ci_rule.c \
 *                        tests/test_lib.c -o t && ./t)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "ci_prng.h"
#include "ci_rdp.h"
#include "ci_dtp.h"
#include "ci_rule.h"

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg) do {                              \
    if (cond) { g_pass++; }                                \
    else { g_fail++; printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

/* ---- RDP trailer (verified: 5-byte big-endian trailer, flags & 0x0F) ---- */

static void test_rdp_parse(void) {
    /* 10-byte buffer; last 5 are the trailer: flags=0x08(SYN), seq=0x002A=42, ack=0x0105=261 */
    uint8_t buf[10] = {0xAA,0xBB,0xCC,0xDD,0xEE, 0x08, 0x00,0x2A, 0x01,0x05};
    ci_rdp_header_t h;
    CHECK(ci_rdp_parse_trailer(buf, sizeof buf, &h) == 0, "rdp parse returns 0");
    CHECK(h.flags == CI_RDP_SYN, "rdp flags = SYN");
    CHECK(h.seq == 42, "rdp seq big-endian decode = 42");
    CHECK(h.ack == 261, "rdp ack big-endian decode = 261");

    /* The mask gotcha: high nibble is an anti-dedup counter. 0x18 = (1<<4)|SYN.
     * Without `& 0x0F` this would read 0x18 and fail SYN classification. */
    uint8_t masked[5] = {0x18, 0x00,0x01, 0x00,0x02};
    ci_rdp_header_t m;
    CHECK(ci_rdp_parse_trailer(masked, sizeof masked, &m) == 0, "rdp parse (mask case)");
    CHECK(m.flags == CI_RDP_SYN, "rdp high-nibble counter masked off -> SYN");
    CHECK((m.flags & CI_RDP_SYN) != 0, "masked flags still classify as SYN");

    /* too short */
    uint8_t tiny[4] = {0,0,0,0};
    CHECK(ci_rdp_parse_trailer(tiny, sizeof tiny, &m) == -1, "rdp len<5 -> -1");
    CHECK(ci_rdp_parse_trailer(NULL, 10, &m) == -1, "rdp NULL data -> -1");

    /* pinned constants */
    CHECK(CI_RDP_HEADER_SIZE == 5, "RDP header size == 5");
    CHECK(CI_CSP_FRDP == 0x02, "CSP_FRDP == 0x02");
}

/* ---- DTP data packet (verified: uint32 LE byte-offset header, frag=off/(mtu-4)) ---- */

static void test_dtp_parse(void) {
    /* offset 64 little-endian, then payload */
    uint8_t buf[8] = {0x40,0x00,0x00,0x00, 0x11,0x22,0x33,0x44};
    uint32_t off = 0;
    CHECK(ci_dtp_parse_offset(buf, sizeof buf, &off) == 0, "dtp offset parse ok");
    CHECK(off == 64, "dtp offset little-endian = 64");

    /* mtu=68 -> useful payload 64 -> fragment index = 64/64 = 1 */
    uint32_t frag = 999;
    CHECK(ci_dtp_fragment_index(64, 68, &frag) == 0, "dtp frag idx ok");
    CHECK(frag == 1, "dtp fragment index = 1 at offset 64, mtu 68");
    CHECK(ci_dtp_fragment_index(0, 68, &frag) == 0 && frag == 0, "dtp frag 0 at offset 0");

    /* mtu too small -> error (need > 4 for useful payload) */
    CHECK(ci_dtp_fragment_index(64, 4, &frag) == -1, "dtp mtu<=4 -> -1");
    CHECK(ci_dtp_parse_offset(buf, 3, &off) == -1, "dtp len<4 -> -1");

    CHECK(CI_DTP_DATA_PORT == 8 && CI_DTP_CONTROL_PORT == 7 && CI_DIPP_META_PORT == 13,
          "DTP/DIPP port constants");
}

/* ---- PRNG (verified: fixed algorithm, deterministic per index) ---- */

static void test_prng(void) {
    /* canonical splitmix64(0) first output -- pins the algorithm across OSes */
    CHECK(ci_splitmix64(0) == 0xE220A8397B1DCDAFULL, "splitmix64(0) known vector");

    /* determinism: same (seed,index) -> same draw */
    CHECK(ci_draw(12345, 7) == ci_draw(12345, 7), "ci_draw deterministic");
    /* different indices generally differ */
    CHECK(ci_draw(12345, 7) != ci_draw(12345, 8), "ci_draw varies by index");
    /* different seeds generally differ */
    CHECK(ci_draw(1, 7) != ci_draw(2, 7), "ci_draw varies by seed");
}

/* ---- drop rule: match + deterministic decision vector (the repro gate) ---- */

static void test_rule_match(void) {
    ci_drop_rule_t r = {0};
    r.match_dport = 13;
    r.match_rdp_syn = 1;

    ci_frame_t syn13  = { .dport = 13, .is_rdp = 1, .rdp_flags = CI_RDP_SYN };
    ci_frame_t ack13  = { .dport = 13, .is_rdp = 1, .rdp_flags = CI_RDP_ACK };
    ci_frame_t data8  = { .dport = 8,  .is_rdp = 0, .rdp_flags = 0 };
    ci_frame_t syn8   = { .dport = 8,  .is_rdp = 1, .rdp_flags = CI_RDP_SYN };

    CHECK(ci_rule_match(&r, &syn13) == 1, "match: RDP SYN on port 13");
    CHECK(ci_rule_match(&r, &ack13) == 0, "no match: RDP non-SYN on 13");
    CHECK(ci_rule_match(&r, &data8) == 0, "no match: non-RDP on 8");
    CHECK(ci_rule_match(&r, &syn8)  == 0, "no match: SYN on wrong port");

    /* port-8 DTP rule (no RDP gating) */
    ci_drop_rule_t r8 = {0};
    r8.match_dport = 8;
    CHECK(ci_rule_match(&r8, &data8) == 1, "match: DTP data on port 8");
    CHECK(ci_rule_match(&r8, &syn13) == 0, "no match: port 13 under port-8 rule");
}

static void test_rule_decide(void) {
    ci_drop_rule_t r = {0};
    r.seed = 0xDEADBEEF;
    r.drop_probability = 0.30;

    /* the reproducibility gate in miniature: two independent passes over the
     * same index range must produce a BYTE-IDENTICAL decision vector. */
    uint8_t vec_a[512], vec_b[512];
    for (uint64_t i = 0; i < 512; i++) vec_a[i] = (uint8_t)ci_rule_decide(&r, i);
    for (uint64_t i = 0; i < 512; i++) vec_b[i] = (uint8_t)ci_rule_decide(&r, i);
    CHECK(memcmp(vec_a, vec_b, sizeof vec_a) == 0, "decision vector reproducible (same seed)");

    /* a different seed should produce a different vector (overwhelmingly likely) */
    ci_drop_rule_t r2 = r; r2.seed = 0xFEEDFACE;
    uint8_t vec_c[512];
    for (uint64_t i = 0; i < 512; i++) vec_c[i] = (uint8_t)ci_rule_decide(&r2, i);
    CHECK(memcmp(vec_a, vec_c, sizeof vec_a) != 0, "different seed -> different vector");

    /* observed drop rate roughly tracks p=0.30 over 512 draws (loose bound) */
    int drops = 0;
    for (uint64_t i = 0; i < 512; i++) drops += vec_a[i];
    CHECK(drops > 100 && drops < 200, "drop rate ~30% over 512 (sanity)");

    /* boundaries */
    ci_drop_rule_t r0 = {0}; r0.drop_probability = 0.0;
    ci_drop_rule_t r1 = {0}; r1.drop_probability = 1.0;
    CHECK(ci_rule_decide(&r0, 42) == 0, "p=0 never drops");
    CHECK(ci_rule_decide(&r1, 42) == 1, "p=1 always drops");

    /* replay vector overrides probability */
    uint8_t replay[4] = {0,1,0,1};
    ci_drop_rule_t rr = {0};
    rr.drop_probability = 0.0;       /* would never drop probabilistically */
    rr.replay_vector = replay;
    rr.replay_len = 4;
    CHECK(ci_rule_decide(&rr, 1) == 1, "replay vector drops index 1");
    CHECK(ci_rule_decide(&rr, 2) == 0, "replay vector keeps index 2");
    CHECK(ci_rule_decide(&rr, 99) == 0, "replay out-of-range keeps");
}

int main(void) {
    test_rdp_parse();
    test_dtp_parse();
    test_prng();
    test_rule_match();
    test_rule_decide();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
