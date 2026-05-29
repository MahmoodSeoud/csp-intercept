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
 * Deterministic drop decision for packet `index` among the in-scope stream.
 * Returns 1 = drop, 0 = keep. Pure function of (rule, index): the vector of
 * decisions over indices is byte-identical across runs and OSes.
 */
int ci_rule_decide(const ci_drop_rule_t *r, uint64_t index);

#endif /* CI_RULE_H */
