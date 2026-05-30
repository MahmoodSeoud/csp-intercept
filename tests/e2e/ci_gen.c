/*
 * ci_gen - deterministic CSP traffic generator for the proxy E2E.
 *
 * Publishes a fixed sequence of CSP v2 frames (port 13 RDP, each carrying a 5-byte
 * RDP trailer with an incrementing seq) to the proxy frontend (XSUB). Because the
 * proxy keys its drop decision on the RDP seq (not arrival order), the same input
 * must yield a byte-identical drop-log across runs with the same seed.
 *
 * Usage: ci_gen <count> <connect-addr> <dport>
 *   defaults: 500  tcp://localhost:6000  13
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <zmq.h>
#include <csp/csp.h>
#include <csp/csp_id.h>

#define CI_CSP_FRDP 0x02   /* id.flags bit marking an RDP packet */
#define CI_RDP_ACK  0x04   /* RDP trailer flag (arbitrary non-SYN payload) */

extern csp_conf_t csp_conf;

int main(int argc, char ** argv) {
    int          count = (argc > 1) ? atoi(argv[1]) : 500;
    const char * addr  = (argc > 2) ? argv[2] : "tcp://localhost:6000";
    int          dport = (argc > 3) ? atoi(argv[3]) : 13;

    csp_conf.version = 2;

    void * ctx = zmq_ctx_new();
    void * pub = zmq_socket(ctx, ZMQ_PUB);
    if (zmq_connect(pub, addr) != 0) {
        return 1;
    }
    /* Allow the PUB->XSUB connection + subscription propagation (ZMQ slow joiner).
     * Generous so a loaded CI runner still has the subscription established before
     * the first send -- otherwise early frames are silently dropped by the PUB. */
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
        p->id.dport = (uint16_t)dport;
        p->id.sport = 40;
        p->id.flags = CI_CSP_FRDP;

        /* payload = 3 bytes + 5-byte big-endian RDP trailer {flags, seq, ack} */
        uint16_t seq = (uint16_t)i;
        p->data[0] = 0xDE;
        p->data[1] = 0xAD;
        p->data[2] = 0xBE;
        p->data[3] = CI_RDP_ACK;            /* trailer flags */
        p->data[4] = (uint8_t)(seq >> 8);   /* seq big-endian */
        p->data[5] = (uint8_t)(seq & 0xFF);
        p->data[6] = 0x00;                  /* ack big-endian */
        p->data[7] = 0x00;
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
