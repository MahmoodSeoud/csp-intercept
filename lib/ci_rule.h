/*
 * ci_rule.h - the CSP-aware drop rule, shared by the monitor APM and the proxy.
 *
 * This is the one source of truth (Eng review decision 4 / DRY). A `ci_frame_t`
 * is the parsed view both front-ends build from the wire; `ci_rule_match` decides
 * if a frame is in scope; `ci_rule_decide` deterministically decides drop/keep for
 * a given packet index (the reproducibility gate -- one splitmix64 draw per index,
 * or an explicit replay vector). See ci_prng.h for why determinism matters.
 */
#ifndef CI_RULE_H
#define CI_RULE_H

#include <stddef.h>
#include <stdint.h>

/* A frame's parsed view (filled from the wire by the APM/proxy). */
typedef struct {
    uint16_t dport;      /* csp id.dport                      */
    uint8_t  csp_flags;  /* csp id.flags                      */
    int      is_rdp;     /* nonzero if csp_flags & CI_CSP_FRDP */
    uint8_t  rdp_flags;  /* masked RDP trailer flags; valid iff is_rdp */
} ci_frame_t;

/*
 * Build a ci_frame_t from primitive wire fields. This is the ONE mapping shared by
 * both front-ends (the monitor APM and the lossy proxy), so the bug-prone "is this
 * RDP, where is the trailer" logic lives in one place (Eng review DRY / decision 4).
 *
 * Kept libcsp-free on purpose: the caller passes the csp id.dport / id.flags and the
 * CSP payload [data, len] (NOT a csp_packet_t). `is_rdp` is set from
 * (csp_flags & CI_CSP_FRDP); when RDP, the 5-byte trailer is parsed and `rdp_flags`
 * is the masked flags (0 if len < CI_RDP_HEADER_SIZE). `data` is not dereferenced
 * when len == 0. Returns 0 on success, -1 if `out` is NULL.
 */
int ci_frame_from_fields(uint16_t dport, uint8_t csp_flags,
                         const uint8_t *data, size_t len, ci_frame_t *out);

typedef struct {
    uint64_t seed;             /* PRNG seed (reproducibility)              */
    int      match_dport;      /* -1 = any port; else match this dport     */
    int      match_rdp_syn;    /* nonzero = only RDP SYN packets           */
    double   drop_probability; /* [0,1]; used when replay_vector == NULL   */

    /* Optional explicit replay: 1 byte (0/1) per packet index. If set, it
     * overrides the probabilistic path -- this is how a recorded real pass
     * drives the simulator. */
    const uint8_t *replay_vector;
    size_t         replay_len;
} ci_drop_rule_t;

/* Returns nonzero if `f` is in scope for this rule (NOT a drop decision). */
int ci_rule_match(const ci_drop_rule_t *r, const ci_frame_t *f);

/*
 * Per-flow reproducibility KEY for RDP flows (ports 7/13): the 16-bit RDP seq
 * number lifted into a 64-bit space by a wrap `epoch` the caller maintains. This
 * is the `index` passed to ci_rule_decide for RDP. (For DTP bulk on port 8 the key
 * is the fragment index offset/(mtu-4) directly.)
 */
static inline uint64_t ci_flow_index_rdp(uint32_t epoch, uint16_t rdp_seq) {
    return ((uint64_t)epoch << 16) | (uint64_t)rdp_seq;
}

/*
 * True when `new_seq` is forward progress that crossed the 16-bit wrap boundary
 * (the raw value went down while the modular distance stayed small) -- the caller
 * should bump its wrap epoch. A small backward REORDER does NOT trip this. Returns
 * 1 = wrapped, 0 = not. (16-bit seq aliases every 65536 packets; the epoch keeps
 * the per-flow identity monotonic past that. See ci_flow_index_rdp.)
 */
int ci_rdp_seq_is_wrap(uint16_t prev_seq, uint16_t new_seq);

/*
 * Stateful per-flow index tracker. Both fault injectors (the ZMQ lossy proxy and the
 * in-path CAN/KISS drop shim) must key their drop decision on the SAME protocol
 * identity, or a frame the proxy would drop and one the shim would drop wouldn't line
 * up. This holds the RDP 16-bit-seq wrap epoch (and the DTP session MTU) so the key is
 * computed identically wherever it is used. Not thread-safe; one tracker per flow path.
 */
typedef struct {
    uint16_t last_seq;       /* last RDP seq seen (for wrap detection)      */
    int      have_last_seq;  /* 0 until the first RDP frame               */
    uint32_t epoch;          /* RDP wrap epoch (bumped on 0xFFFF->0 cross) */
    uint16_t dtp_mtu;        /* DTP session MTU for fragment indexing      */
} ci_flow_tracker_t;

/* Initialise a tracker. `dtp_mtu` is the negotiated DTP session MTU (e.g. 200). */
void ci_flow_tracker_init(ci_flow_tracker_t *t, uint16_t dtp_mtu);

/*
 * Compute the per-flow drop key for a parsed frame, advancing the RDP wrap epoch as a
 * side effect. RDP frames (ports 7/13) -> ci_flow_index_rdp(epoch, seq); DTP bulk
 * (port 8) -> fragment index offset/(mtu-4). `data`/`len` are the CSP payload (the same
 * bytes ci_frame_from_fields saw). Mirrors the proxy's proxy_flow_index exactly so the
 * proxy and the shim are interchangeable oracles. Returns 0 if the trailer/offset can't
 * be parsed (treated as flow index 0, same as the proxy).
 */
uint64_t ci_flow_index(ci_flow_tracker_t *t, const ci_frame_t *f,
                       const uint8_t *data, size_t len);

/*
 * Deterministic drop decision for the packet whose per-flow PROTOCOL IDENTITY is
 * `index`. Returns 1 = drop, 0 = keep.
 *
 * `index` is NOT a wire/arrival position. Arrival order is nondeterministic across
 * runs (ZMQ interleave, scheduling, RDP retransmits), so keying the PRNG on it would
 * make the "reproducible" drop vector differ every run. Instead the caller derives
 * `index` from a packet-intrinsic field: the RDP seq via ci_flow_index_rdp() for RDP
 * flows (ports 7/13), or the DTP fragment index offset/(mtu-4) for the port-8 bulk
 * stream. Keyed this way the decision vector is byte-identical across runs and OSes,
 * and a recorded-pass replay vector aligns by that same identity.
 * (Eng review 2026-05-30, Tension 1.)
 */
int ci_rule_decide(const ci_drop_rule_t *r, uint64_t index);

#endif /* CI_RULE_H */
