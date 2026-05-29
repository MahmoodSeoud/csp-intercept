#include "ci_rule.h"
#include "ci_rdp.h"
#include "ci_prng.h"

int ci_rule_match(const ci_drop_rule_t *r, const ci_frame_t *f) {
    if (r == NULL || f == NULL) {
        return 0;
    }
    if (r->match_dport >= 0 && f->dport != (uint16_t)r->match_dport) {
        return 0;
    }
    if (r->match_rdp_syn) {
        if (!f->is_rdp) {
            return 0;
        }
        if (!(f->rdp_flags & CI_RDP_SYN)) {
            return 0;
        }
    }
    return 1;
}

int ci_rule_decide(const ci_drop_rule_t *r, uint64_t index) {
    if (r == NULL) {
        return 0;
    }

    /* Explicit replay (recorded pass) wins. Out-of-range indices keep. */
    if (r->replay_vector != NULL) {
        if (index >= r->replay_len) {
            return 0;
        }
        return r->replay_vector[index] ? 1 : 0;
    }

    /* Probabilistic, but deterministic per index: one draw, compare to a
     * threshold = p * 2^64. p<=0 -> never drop; p>=1 -> always drop. */
    if (r->drop_probability <= 0.0) {
        return 0;
    }
    if (r->drop_probability >= 1.0) {
        return 1;
    }
    uint64_t threshold = (uint64_t)(r->drop_probability * 18446744073709551616.0);
    return ci_draw(r->seed, index) < threshold ? 1 : 0;
}
