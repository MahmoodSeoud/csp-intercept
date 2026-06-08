/*
 * ci_gen_dtp - deterministic DTP (port 8) traffic generator for the two-oracle E2E.
 *
 * The DTP counterpart of ci_gen. Publishes a fixed sequence of CSP v2 frames on the
 * DTP bulk data port (8), each carrying the libdtp data-plane header: a little-endian
 * uint32 byte offset as the first 4 bytes of the CSP payload, and NO RDP trailer
 * (DTP is connectionless/unreliable -- see lib/ci_dtp.h provenance). Fragment i has
 * byte offset i*(mtu-4), so the proxy's and APM's fragment index (offset/(mtu-4))
 * is exactly i. Because both oracles key on that protocol-intrinsic fragment index
 * (not wire arrival order), the same input yields a byte-identical drop decision
 * across runs with the same seed.
 *
 * Usage: ci_gen_dtp <count> <connect-addr> <mtu> [overhead]
 *   defaults: 500  tcp://localhost:6000  200  4   (port is always 8, the DTP data port)
 *
 * `overhead` is the libdtp data-header size: 4 (dipp, default) or 8 (satDeploy). The
 * leading uint32 byte-offset is identical in both; only the per-fragment payload span
 * (mtu - overhead) -- and thus offset i*(mtu-overhead) -> fragment index i -- differs.
 * Pass 8 (with the proxy -H 8 and the APM -O 8) to exercise the satDeploy parse path.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <zmq.h>
#include <csp/csp.h>
#include <csp/csp_id.h>

#include "ci_dtp.h"   /* CI_DTP_DATA_PORT */

extern csp_conf_t csp_conf;

int main(int argc, char ** argv) {
    int          count    = (argc > 1) ? atoi(argv[1]) : 500;
    const char * addr     = (argc > 2) ? argv[2] : "tcp://localhost:6000";
    int          mtu      = (argc > 3) ? atoi(argv[3]) : 200;
    int          overhead = (argc > 4) ? atoi(argv[4]) : CI_DTP_OVERHEAD_DIPP;

    if (overhead < CI_DTP_OFFSET_SIZE || mtu <= overhead) {
        return 2;   /* (mtu-overhead) must be positive: it is the per-fragment payload span */
    }
    const uint32_t step = (uint32_t)(mtu - overhead);   /* byte offset increment per fragment */

    csp_conf.version = 2;

    void * ctx = zmq_ctx_new();
    void * pub = zmq_socket(ctx, ZMQ_PUB);
    if (zmq_connect(pub, addr) != 0) {
        return 1;
    }
    /* Allow the PUB->XSUB connection + subscription propagation (ZMQ slow joiner). */
    usleep(800000);

    csp_packet_t * p = malloc(sizeof(*p));
    if (p == NULL) {
        return 1;
    }

    for (int i = 0; i < count; i++) {
        memset(&p->id, 0, sizeof(p->id));
        p->id.pri   = 2;
        p->id.src   = 10;
        p->id.dst   = 20;
        p->id.dport = CI_DTP_DATA_PORT;     /* 8: DTP bulk data */
        p->id.sport = 40;
        p->id.flags = 0;                    /* NOT RDP: no CSP_FRDP (0x02) bit */

        /* payload = 4-byte little-endian byte offset + a small data chunk. The
         * fragment index the oracles derive is offset/(mtu-4) == i. */
        uint32_t off = (uint32_t)i * step;
        p->data[0] = (uint8_t)(off & 0xFF);
        p->data[1] = (uint8_t)((off >> 8)  & 0xFF);
        p->data[2] = (uint8_t)((off >> 16) & 0xFF);
        p->data[3] = (uint8_t)((off >> 24) & 0xFF);
        p->data[4] = 0xDE;
        p->data[5] = 0xAD;
        p->data[6] = 0xBE;
        p->data[7] = 0xEF;
        p->length  = 8;

        csp_id_prepend(p);
        zmq_send(pub, p->frame_begin, p->frame_length, 0);
        usleep(500);
    }

    usleep(200000);   /* let the last frames drain through the proxy */
    free(p);
    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    return 0;
}
