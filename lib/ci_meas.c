#include "ci_meas.h"

#include <string.h>

/* ---- sequence tracker --------------------------------------------------- *
 *
 * Unwrapped seq space (W = CI_SEQ_WIN):
 *
 *      first              max
 *        |  . X . X X .  X |          '.' = hole (lost so far)
 *        +----------------+           'X' = seen (in `seen` bitmap, circular)
 *        loss = (max-first+1) - distinct_seen
 *
 * A forward feed past `max` exposes new holes (cleared from the circular bitmap
 * so stale bits W positions back cannot read as "seen"). A feed at/below `max`
 * is a duplicate if its bit is set, else a reorder that backfills a hole.
 */

static int seq_bit_get(const ci_seq_tracker_t *t, uint64_t pos) {
    uint32_t i = (uint32_t)(pos % CI_SEQ_WIN);
    return (t->seen[i >> 3] >> (i & 7)) & 1;
}

static void seq_bit_set(ci_seq_tracker_t *t, uint64_t pos, int v) {
    uint32_t i = (uint32_t)(pos % CI_SEQ_WIN);
    if (v) {
        t->seen[i >> 3] |= (uint8_t)(1u << (i & 7));
    } else {
        t->seen[i >> 3] &= (uint8_t)~(1u << (i & 7));
    }
}

void ci_seq_tracker_init(ci_seq_tracker_t *t) {
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

int ci_seq_tracker_feed(ci_seq_tracker_t *t, uint16_t seq) {
    if (t == NULL) {
        return 0;
    }
    t->n_seen++;

    if (!t->have) {
        t->have       = 1;
        t->first      = (uint64_t)seq;   /* epoch 0 baseline */
        t->max        = (uint64_t)seq;
        t->n_distinct = 1;
        seq_bit_set(t, t->max, 1);
        t->epoch = (uint32_t)(t->max >> 16);
        return 0; /* in-order */
    }

    /* Signed 16-bit distance from the current head; valid while |reorder| < 2^15. */
    int16_t  diff      = (int16_t)((uint16_t)seq - (uint16_t)(t->max & 0xFFFFu));
    uint64_t unwrapped = (uint64_t)((int64_t)t->max + diff);

    if (diff > 0) {
        /* Forward: clear newly-exposed holes (bounded by the window), mark seen. */
        uint64_t clear_from = t->max + 1;
        if (unwrapped - t->max > CI_SEQ_WIN) {
            clear_from = unwrapped - CI_SEQ_WIN;
        }
        for (uint64_t p = clear_from; p < unwrapped; p++) {
            seq_bit_set(t, p, 0);
        }
        seq_bit_set(t, unwrapped, 1);
        t->max = unwrapped;
        t->n_distinct++;
        t->epoch = (uint32_t)(t->max >> 16);
        if (diff > 1) {
            t->n_gap += (uint64_t)(diff - 1);
            return 1; /* forward gap */
        }
        return 0; /* in-order */
    }

    /* diff <= 0: at or behind max -> duplicate or reorder backfill. */
    if (seq_bit_get(t, unwrapped)) {
        t->n_dup++;
        return 2; /* duplicate */
    }
    seq_bit_set(t, unwrapped, 1);
    t->n_distinct++;
    t->n_reorder++;
    return 3; /* reorder */
}

uint64_t ci_seq_tracker_loss(const ci_seq_tracker_t *t) {
    if (t == NULL || !t->have) {
        return 0;
    }
    uint64_t span = t->max - t->first + 1;
    return span > t->n_distinct ? span - t->n_distinct : 0;
}

int ci_loss_trustworthy(const ci_seq_tracker_t *t) {
    if (t == NULL || !t->have) {
        return 1; /* no packets fed -> no loss claim to distrust */
    }
    uint64_t span = t->max - t->first + 1;
    if (span < CI_LOSS_MIN_SPAN) {
        return 1; /* tiny span -> (span - distinct) is fine as-is */
    }
    /* Dense enough? saw >= span/DIV distinct seqs. Cross-multiply so we never
     * divide (avoids truncation and a div-by-zero on a zero DIV misconfig). */
    return (t->n_distinct * (uint64_t)CI_LOSS_DENSITY_DIV >= span) ? 1 : 0;
}

/* ---- RTT pairing -------------------------------------------------------- */

void ci_rtt_init(ci_rtt_pairing_t *p) {
    if (p != NULL) {
        memset(p, 0, sizeof(*p));
    }
}

void ci_rtt_on_data(ci_rtt_pairing_t *p, uint16_t seq, uint32_t t_ms) {
    if (p == NULL) {
        return;
    }
    uint32_t i = p->head % CI_RTT_RING;
    p->seq[i]   = seq;
    p->t_ms[i]  = t_ms;
    p->valid[i] = 1;
    p->head++;
}

int ci_rtt_on_ack(ci_rtt_pairing_t *p, uint16_t ack_nr, uint32_t t_ms,
                  uint32_t *rtt_ms_out) {
    if (p == NULL) {
        return 0;
    }
    /* Search newest-first so an ACK pairs with the most recent matching data. */
    for (uint32_t k = 0; k < CI_RTT_RING; k++) {
        uint32_t i = (p->head - 1 - k) % CI_RTT_RING;
        if (p->valid[i] && p->seq[i] == ack_nr) {
            if (rtt_ms_out != NULL) {
                *rtt_ms_out = t_ms - p->t_ms[i];
            }
            p->valid[i] = 0; /* consume */
            return 1;
        }
    }
    return 0;
}

/* ---- measurement-suspect window flag ------------------------------------ */

uint8_t ci_u8_delta(uint8_t prev, uint8_t cur) {
    return (uint8_t)(cur - prev); /* wraps mod 256, matching the uint8 counters */
}

int ci_measurement_suspect(const ci_window_health_t *h, uint32_t *flags_out) {
    uint32_t f = 0;
    if (h == NULL) {
        if (flags_out != NULL) {
            *flags_out = 0;
        }
        return 0;
    }
    if (h->conn_ovf_delta > 0) {
        f |= CI_SUSPECT_CONN_OVF;
    }
    if (h->buffer_out_delta > 0) {
        f |= CI_SUSPECT_BUFFER_OUT;
    }
    if (h->buffer_remaining < h->buffer_low_water) {
        f |= CI_SUSPECT_BUFFER_LOW;
    }
    if (flags_out != NULL) {
        *flags_out = f;
    }
    return f != 0 ? 1 : 0;
}
