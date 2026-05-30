/*
 * csp_monitor_apm.c - CSH APM: a promiscuous CSP link monitor for csp-intercept.
 *
 * `csp_monitor start` enables libcsp promiscuous mode and spawns a background
 * drainer that, for traffic matching a target dport, parses the RDP trailer (ports
 * 7/13) or DTP offset (port 8) via the shared lib, infers loss/dup/reorder + an
 * observed-at-tap RTT (lib/ci_meas), and appends a frozen-schema CSV row. Each
 * window it writes a #WINDOW summary carrying a MEASUREMENT_SUSPECT flag so
 * instrument loss (promisc queue overflow / buffer-pool exhaustion) is never
 * mistaken for link loss. `csp_monitor stop` joins the drainer and closes the CSV.
 *
 * Design decisions (eng review 2026-05-30):
 *  - Background detached-style thread (start returns immediately; the prompt stays
 *    free so a dtp_client download can run in the same csh). Stop joins it.
 *  - SINGLE drainer + setvbuf'd FILE* + per-window fflush (no ring/2nd thread) --
 *    at UHF/KB-s rates the 1000-deep promisc queue has ample headroom (T2).
 *  - The drop/measurement math lives in the pure lib (ci_meas), unit-tested.
 *  - conn_rxqueue_len is a build-time constant (csp_promisc_enable ignores its arg);
 *    we log it and warn if it was built too small.
 *  - Promisc clones carry the RDP trailer and a populated id (the promisc hook fires
 *    before the trailer is stripped), so no csp_id_strip is needed here.
 *  - v1 contract: do NOT run another promiscuous consumer (param_sniffer / VM)
 *    concurrently -- the promisc queue is single-consumer and they'd steal packets.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include <apm/apm.h>
#include <slash/slash.h>
#include <slash/optparse.h>
#include <csp/csp.h>
#include <csp/csp_buffer.h>
#include <csp/csp_promisc.h>
#include <csp/csp_debug.h>
#include <csp/arch/csp_time.h>

#include "ci_rule.h"
#include "ci_rdp.h"
#include "ci_dtp.h"
#include "ci_meas.h"

/* --- monitor state (persists across slash invocations) --- */
static pthread_t mon_thread;
static volatile int mon_running = 0;
static FILE *  mon_csv = NULL;
static char    mon_csv_path[256] = "csp-intercept.csv";
static int     mon_match_dport = CI_DIPP_META_PORT;   /* default: port 13 RDP; -1 = any */
static uint32_t mon_window_ms = 1000;
static uint16_t mon_mtu = 200;
static int     mon_buffer_low_water = 100;

/* drainer-thread-only measurement state */
static ci_seq_tracker_t mon_seq;
static ci_rtt_pairing_t mon_rtt;

static void mon_write_header(void) {
    fprintf(mon_csv,
        "# per-packet: t_ms,src,dst,dport,csp_flags,is_rdp,rdp_flags,rdp_seq,rdp_ack,dtp_offset,dtp_frag,rtt_paired,rtt_ms\n");
    fprintf(mon_csv,
        "# window: #WINDOW,t_ms,conn_ovf_delta,buffer_out_delta,buffer_remaining,inferred_loss,dups,reorders,suspect,suspect_flags\n");
}

static void *mon_drainer(void *arg) {
    (void)arg;
    ci_seq_tracker_init(&mon_seq);
    ci_rtt_init(&mon_rtt);

    uint8_t  prev_ovf  = csp_dbg_conn_ovf;
    uint8_t  prev_bout = csp_dbg_buffer_out;
    uint32_t window_start = csp_get_ms();

    while (mon_running) {
        /* 100 ms timeout so the loop re-checks mon_running promptly on stop. */
        csp_packet_t *pk = csp_promisc_read(100);
        if (pk != NULL) {
            uint32_t t = csp_get_ms();
            if (mon_match_dport < 0 || pk->id.dport == (uint16_t)mon_match_dport) {
                ci_frame_t f;
                ci_frame_from_fields(pk->id.dport, pk->id.flags, pk->data, pk->length, &f);
                if (f.is_rdp) {
                    ci_rdp_header_t h;
                    int ok = (ci_rdp_parse_trailer(pk->data, pk->length, &h) == 0);
                    uint32_t rtt = 0;
                    int paired = 0;
                    if (ok) {
                        ci_seq_tracker_feed(&mon_seq, h.seq);
                        ci_rtt_on_data(&mon_rtt, h.seq, t);
                        paired = ci_rtt_on_ack(&mon_rtt, h.ack, t, &rtt);
                    }
                    fprintf(mon_csv, "%u,%u,%u,%u,0x%02X,1,0x%02X,%u,%u,,,%d,%u\n",
                            t, pk->id.src, pk->id.dst, pk->id.dport, pk->id.flags,
                            ok ? h.flags : 0, ok ? h.seq : 0, ok ? h.ack : 0,
                            paired, paired ? rtt : 0);
                } else if (pk->id.dport == CI_DTP_DATA_PORT) {
                    uint32_t off = 0, frag = 0;
                    if (ci_dtp_parse_offset(pk->data, pk->length, &off) == 0) {
                        ci_dtp_fragment_index(off, mon_mtu, &frag);
                    }
                    fprintf(mon_csv, "%u,%u,%u,%u,0x%02X,0,,,,%u,%u,,\n",
                            t, pk->id.src, pk->id.dst, pk->id.dport, pk->id.flags, off, frag);
                }
            }
            csp_buffer_free(pk);
        }

        uint32_t now = csp_get_ms();
        if (now - window_start >= mon_window_ms) {
            ci_window_health_t hw;
            hw.conn_ovf_delta   = ci_u8_delta(prev_ovf, csp_dbg_conn_ovf);
            hw.buffer_out_delta = ci_u8_delta(prev_bout, csp_dbg_buffer_out);
            hw.buffer_remaining = csp_buffer_remaining();
            hw.buffer_low_water = mon_buffer_low_water;
            uint32_t flags = 0;
            int suspect = ci_measurement_suspect(&hw, &flags);
            fprintf(mon_csv, "#WINDOW,%u,%u,%u,%d,%llu,%llu,%llu,%d,0x%X\n",
                    now, hw.conn_ovf_delta, hw.buffer_out_delta, hw.buffer_remaining,
                    (unsigned long long)ci_seq_tracker_loss(&mon_seq),
                    (unsigned long long)mon_seq.n_dup,
                    (unsigned long long)mon_seq.n_reorder,
                    suspect, flags);
            fflush(mon_csv);
            prev_ovf  = csp_dbg_conn_ovf;
            prev_bout = csp_dbg_buffer_out;
            window_start = now;
        }
    }
    return NULL;
}

static int csp_monitor_start_cmd(struct slash *slash) {
    if (mon_running) {
        printf("csp_monitor: already running -> %s\n", mon_csv_path);
        return SLASH_SUCCESS;
    }

    char *   out    = NULL;
    int      dport  = mon_match_dport;
    unsigned window = mon_window_ms;
    unsigned mtu    = mon_mtu;

    optparse_t *p = optparse_new("csp_monitor start", "");
    optparse_add_help(p);
    optparse_add_string(p, 'o', "out", "FILE", &out, "CSV output (default csp-intercept.csv)");
    optparse_add_int(p, 'd', "dport", "PORT", 10, &dport, "match dport (default 13; -1 = any)");
    optparse_add_unsigned(p, 'w', "window", "MS", 10, &window, "window length ms (default 1000)");
    optparse_add_unsigned(p, 'm', "mtu", "BYTES", 10, &mtu, "DTP session MTU (default 200)");
    int argi = optparse_parse(p, slash->argc - 1, (const char **)slash->argv + 1);
    if (argi < 0) {
        optparse_del(p);
        return SLASH_EINVAL;
    }
    optparse_del(p);

    if (out) {
        strncpy(mon_csv_path, out, sizeof(mon_csv_path) - 1);
        mon_csv_path[sizeof(mon_csv_path) - 1] = '\0';
    }
    mon_match_dport = dport;
    mon_window_ms   = window;
    mon_mtu         = (uint16_t)mtu;

    int rc = csp_promisc_enable(CSP_CONN_RXQUEUE_LEN);
    if (rc != CSP_ERR_NONE && rc != CSP_ERR_USED) {
        printf("csp_monitor: promisc enable failed (%d)\n", rc);
        return SLASH_EINVAL;
    }
    printf("csp_monitor: promisc queue depth = %d (build-time CSP_CONN_RXQUEUE_LEN)\n",
           CSP_CONN_RXQUEUE_LEN);
    if (CSP_CONN_RXQUEUE_LEN < 1000) {
        printf("csp_monitor: WARNING queue depth < 1000 -> instrument loss likely; "
               "rebuild with -Dcsp:conn_rxqueue_len=1000\n");
    }

    mon_csv = fopen(mon_csv_path, "w");
    if (mon_csv == NULL) {
        printf("csp_monitor: cannot open %s\n", mon_csv_path);
        return SLASH_EIO;
    }
    setvbuf(mon_csv, NULL, _IOFBF, 1 << 16);
    mon_write_header();

    mon_running = 1;
    if (pthread_create(&mon_thread, NULL, mon_drainer, NULL) != 0) {
        mon_running = 0;
        fclose(mon_csv);
        mon_csv = NULL;
        printf("csp_monitor: failed to start drainer thread\n");
        return SLASH_ENOMEM;
    }
    printf("csp_monitor: started -> %s (dport=%d, window=%ums, mtu=%u)\n",
           mon_csv_path, mon_match_dport, mon_window_ms, mon_mtu);
    return SLASH_SUCCESS;
}
slash_command_sub(csp_monitor, start, csp_monitor_start_cmd,
                  "[-o FILE] [-d DPORT] [-w MS] [-m MTU]",
                  "Start the promiscuous CSP link monitor");

static int csp_monitor_stop_cmd(struct slash *slash) {
    (void)slash;
    if (!mon_running) {
        printf("csp_monitor: not running\n");
        return SLASH_SUCCESS;
    }
    mon_running = 0;                 /* drainer exits within ~100 ms (read timeout) */
    pthread_join(mon_thread, NULL);  /* race-free: no writes to mon_csv after this  */
    if (mon_csv != NULL) {
        fflush(mon_csv);
        fclose(mon_csv);
        mon_csv = NULL;
    }
    printf("csp_monitor: stopped -> %s\n", mon_csv_path);
    return SLASH_SUCCESS;
}
slash_command_sub(csp_monitor, stop, csp_monitor_stop_cmd, "",
                  "Stop the link monitor and close the CSV");

int apm_init(void) {
    return 0;
}
