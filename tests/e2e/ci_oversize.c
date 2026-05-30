/*
 * ci_oversize - frame-size bound-guard generator for the proxy E2E.
 *
 * Publishes `conforming` well-formed CSP v2 RDP frames (port 13, 8 bytes each, like
 * ci_gen) interleaved with `oversize` deliberately malformed frames larger than the
 * proxy's reusable packet buffer (sizeof(data)+HEADER_SIZE = 2054). The oversized
 * frames exercise the proxy's recv->memcpy bound guard (TODOS#4): with the guard they
 * are counted as malformed and dropped; without it, datalen is copied unchecked into
 * a fixed csp_packet_t and smashes the heap.
 *
 * Usage: ci_oversize <conforming> <oversize> <connect-addr> <dport>
 *   defaults: 200  4  tcp://localhost:6000  13
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <zmq.h>
#include <csp/csp.h>
#include <csp/csp_id.h>

#define CI_CSP_FRDP 0x02
#define CI_RDP_ACK  0x04
#define OVERSIZE_LEN 4096   /* > 2054: forces the bound guard to trip */

extern csp_conf_t csp_conf;

int main(int argc, char ** argv) {
    int          conforming = (argc > 1) ? atoi(argv[1]) : 200;
    int          oversize   = (argc > 2) ? atoi(argv[2]) : 4;
    const char * addr       = (argc > 3) ? argv[3] : "tcp://localhost:6000";
    int          dport      = (argc > 4) ? atoi(argv[4]) : 13;

    csp_conf.version = 2;

    void * ctx = zmq_ctx_new();
    void * pub = zmq_socket(ctx, ZMQ_PUB);
    if (zmq_connect(pub, addr) != 0) {
        return 1;
    }
    usleep(800000);   /* slow-joiner warmup so no conforming frame is lost (see ci_gen) */

    csp_packet_t * p = malloc(sizeof(*p));
    if (p == NULL) {
        return 1;
    }
    uint8_t * big = malloc(OVERSIZE_LEN);
    if (big == NULL) {
        return 1;
    }
    memset(big, 0xFF, OVERSIZE_LEN);

    /* Spread the oversized frames evenly through the conforming stream so the test
     * also proves the proxy recovers and keeps forwarding after rejecting one. */
    int step = (oversize > 0) ? (conforming / (oversize + 1)) : conforming + 1;
    if (step < 1) {
        step = 1;
    }
    int sent_oversize = 0;

    for (int i = 0; i < conforming; i++) {
        memset(&p->id, 0, sizeof(p->id));
        p->id.pri   = 2;
        p->id.src   = 10;
        p->id.dst   = 20;
        p->id.dport = (uint16_t)dport;
        p->id.sport = 40;
        p->id.flags = CI_CSP_FRDP;

        uint16_t seq = (uint16_t)i;
        p->data[0] = 0xDE;
        p->data[1] = 0xAD;
        p->data[2] = 0xBE;
        p->data[3] = CI_RDP_ACK;
        p->data[4] = (uint8_t)(seq >> 8);
        p->data[5] = (uint8_t)(seq & 0xFF);
        p->data[6] = 0x00;
        p->data[7] = 0x00;
        p->length  = 8;

        csp_id_prepend(p);
        zmq_send(pub, p->frame_begin, p->frame_length, 0);
        usleep(500);

        if (sent_oversize < oversize && ((i + 1) % step == 0)) {
            zmq_send(pub, big, OVERSIZE_LEN, 0);   /* malformed: must NOT crash the proxy */
            sent_oversize++;
            usleep(500);
        }
    }
    /* Send any remaining oversized frames (if step math left some). */
    while (sent_oversize < oversize) {
        zmq_send(pub, big, OVERSIZE_LEN, 0);
        sent_oversize++;
        usleep(500);
    }

    usleep(200000);   /* let the last frames drain through the proxy */
    free(big);
    free(p);
    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    return 0;
}
