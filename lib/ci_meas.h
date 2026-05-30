/*
 * ci_meas.h - the measurement math: the analysis layer of the monitor, kept as
 * PURE functions so it is unit-testable with a bare C compiler (no libcsp, no
 * threads, no I/O). The APM fills these from parsed wire fields; this file owns
 * the bug-prone parts (16-bit RDP seq wrap, dup/reorder classification, the
 * instrument-loss "is this window trustworthy" decision). (Eng review 2026-05-30,
 * "measurement math as pure lib functions".)
 *
 * Why this is separate from parsing: a wrong loss number is a SILENT failure - it
 * does not crash, it just poisons a thesis result. So every branch here gets a test.
 */
#ifndef CI_MEAS_H
#define CI_MEAS_H

#include <stdint.h>

/* ------------------------------------------------------------------------- *
 * Sequence tracker: loss-from-gaps, duplicates, reorders, on a 16-bit RDP seq.
 *
 * RDP seq is uint16 and wraps every 65536 packets; this tracker unwraps it into a
 * 64-bit monotonic space (assuming |reorder distance| < 32768, which holds because
 * the RDP window is <= 1000). A circular "seen" bitmap distinguishes a reorder that
 * BACKFILLS a hole (loss goes down) from a true DUPLICATE retransmit.
 *
 *   loss-so-far = (highest_seen - first_seen + 1) - distinct_seen
 *
 * so a later reorder that fills a gap correctly reduces the inferred loss.
 * ------------------------------------------------------------------------- */
#define CI_SEQ_WIN 2048   /* circular seen-window, >> RDP max window (1000) */

typedef struct {
    int      have;          /* 0 until the first feed                          */
    uint64_t first;         /* first unwrapped seq seen                        */
    uint64_t max;           /* highest unwrapped seq seen                      */
    uint64_t n_seen;        /* total feeds, including duplicates               */
    uint64_t n_distinct;    /* distinct seqs seen (drives loss)                */
    uint64_t n_dup;         /* duplicate (retransmit) count                    */
    uint64_t n_reorder;     /* out-of-order backfills                          */
    uint64_t n_gap;         /* cumulative forward-gap packets (informational)  */
    uint32_t epoch;         /* max >> 16; the wrap epoch (proxy flow-index key) */
    uint8_t  seen[CI_SEQ_WIN / 8];
} ci_seq_tracker_t;

void     ci_seq_tracker_init(ci_seq_tracker_t *t);
/* Feed one observed seq. Returns the classification:
 *   0 = in-order, 1 = forward gap, 2 = duplicate, 3 = reorder (backfill). */
int      ci_seq_tracker_feed(ci_seq_tracker_t *t, uint16_t seq);
/* Inferred loss so far = expected span minus distinct seen (>= 0). */
uint64_t ci_seq_tracker_loss(const ci_seq_tracker_t *t);

/* ------------------------------------------------------------------------- *
 * Observed-at-tap RTT pairing: match a data packet's seq to the ACK whose
 * ack_nr names it, returning the at-tap interval. Bounded ring (oldest dropped).
 * This is the SYN->SYN-ACK / data->ACK interval AS SEEN AT THE TAP, not the true
 * endpoint RTT - the APM must label it as such.
 * ------------------------------------------------------------------------- */
#define CI_RTT_RING 256

typedef struct {
    uint16_t seq[CI_RTT_RING];
    uint32_t t_ms[CI_RTT_RING];
    uint8_t  valid[CI_RTT_RING];
    uint32_t head;            /* monotonic write cursor (index = head % RING)  */
} ci_rtt_pairing_t;

void ci_rtt_init(ci_rtt_pairing_t *p);
void ci_rtt_on_data(ci_rtt_pairing_t *p, uint16_t seq, uint32_t t_ms);
/* If a recorded data packet matches `ack_nr`, write the at-tap RTT and return 1
 * (consuming the entry); else return 0. */
int  ci_rtt_on_ack(ci_rtt_pairing_t *p, uint16_t ack_nr, uint32_t t_ms,
                   uint32_t *rtt_ms_out);

/* ------------------------------------------------------------------------- *
 * MEASUREMENT_SUSPECT window flag: instrument loss must never be mistaken for
 * link loss. Two independent instrument-loss paths exist in libcsp promisc:
 *   - queue overflow  -> csp_dbg_conn_ovf++   (counted)
 *   - buffer-pool exhaustion (clone NULL) -> csp_dbg_buffer_out++ (the SILENT one
 *     the eng review surfaced, #3) and/or csp_buffer_remaining() running low.
 * A window is suspect if ANY of these fired. The counters are uint8 and WRAP at
 * 256, so deltas are computed modulo 256 (ci_u8_delta) and the APM keeps windows
 * short and treats any nonzero delta as suspect.
 * ------------------------------------------------------------------------- */
#define CI_SUSPECT_CONN_OVF   0x01
#define CI_SUSPECT_BUFFER_OUT 0x02
#define CI_SUSPECT_BUFFER_LOW 0x04

typedef struct {
    uint8_t conn_ovf_delta;    /* csp_dbg_conn_ovf delta over the window   */
    uint8_t buffer_out_delta;  /* csp_dbg_buffer_out delta over the window */
    int     buffer_remaining;  /* csp_buffer_remaining() at window close    */
    int     buffer_low_water;  /* threshold below which we flag             */
} ci_window_health_t;

/* mod-256 delta of two wrapping uint8 counters. */
uint8_t ci_u8_delta(uint8_t prev, uint8_t cur);
/* Set *flags_out to the OR of the CI_SUSPECT_* bits; return 1 if any set. */
int     ci_measurement_suspect(const ci_window_health_t *h, uint32_t *flags_out);

#endif /* CI_MEAS_H */
