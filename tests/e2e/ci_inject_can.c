/*
 * ci_inject_can - the injecting end of the CAN two-oracle bench.
 *
 * Opens a real socketcan interface (a sandbox vcan0, NEVER a live bus), wraps it with
 * the in-path drop shim (inject/ci_drop_iface), and transmits a deterministic RDP or
 * DTP stream THROUGH the shim onto the wire. The shim drops a reproducible subset by
 * per-flow identity and logs every decision (oracle A); the kept frames go out on CAN
 * for the monitor node to observe (oracle B). This is the CAN analogue of ci_gen, with
 * the loss injected in-path instead of by a broker.
 *
 * Usage: ci_inject_can <device> <node_addr> <dst> <dport> <count> <mtu> <loss> <seed> <drop.csv>
 *   e.g. ci_inject_can vcan0 10 20 13 500 200 0.3 1 /tmp/can_drop.csv
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <csp/csp.h>
#include <csp/csp_buffer.h>
#include <csp/csp_iflist.h>
#include <csp/drivers/can_socketcan.h>

#include "ci_drop_iface.h"
#include "ci_rule.h"
#include "ci_dtp.h"

#define CI_CSP_FRDP 0x02
#define CI_RDP_ACK  0x04

extern csp_conf_t csp_conf;

int main(int argc, char **argv) {
    const char *dev   = (argc > 1) ? argv[1] : "vcan0";
    uint16_t    addr  = (uint16_t)((argc > 2) ? atoi(argv[2]) : 10);
    uint16_t    dst   = (uint16_t)((argc > 3) ? atoi(argv[3]) : 20);
    int         dport = (argc > 4) ? atoi(argv[4]) : 13;
    int         count = (argc > 5) ? atoi(argv[5]) : 500;
    int         mtu   = (argc > 6) ? atoi(argv[6]) : 200;
    double      loss  = (argc > 7) ? atof(argv[7]) : 0.3;
    uint64_t    seed  = (argc > 8) ? strtoull(argv[8], NULL, 10) : 1;
    const char *log   = (argc > 9) ? argv[9] : NULL;

    csp_conf.version = 2;
    csp_init();

    /* bitrate 0 => csp_can_socketcan skips can_set_bitrate/can_do_start, which a vcan
     * device does not support. The caller must have brought vcan0 up already. promisc
     * here is irrelevant for TX, but harmless. */
    csp_iface_t *can_iface = NULL;
    int rc = csp_can_socketcan_open_and_add_interface(dev, "CANTX", addr, 0, false, &can_iface);
    if (rc != CSP_ERR_NONE || can_iface == NULL) {
        fprintf(stderr, "ci_inject_can: open %s failed rc=%d\n", dev, rc);
        return 1;
    }

    FILE *drop_log = NULL;
    if (log) {
        drop_log = fopen(log, "w");
        if (drop_log == NULL) { fprintf(stderr, "ci_inject_can: cannot open %s\n", log); return 1; }
        fprintf(drop_log, "# t_ms,src,dport,csp_flags,is_rdp,index,epoch,dropped\n");
    }

    ci_drop_rule_t rule = {0};
    rule.seed = seed;
    rule.match_dport = dport;
    rule.drop_probability = loss;

    ci_drop_iface_t shim;
    ci_drop_iface_init(&shim, "CANDROP", can_iface, &rule, (uint16_t)mtu, drop_log);

    usleep(300000);   /* let the CAN socket settle before TX */

    const uint32_t step = (uint32_t)(mtu - 4);
    csp_packet_t *p = csp_buffer_get(0);
    for (int i = 0; i < count; i++) {
        if (p == NULL) { p = csp_buffer_get(0); if (p == NULL) break; }
        memset(&p->id, 0, sizeof(p->id));
        p->id.pri = 2; p->id.src = addr; p->id.dst = dst;
        p->id.dport = (uint16_t)dport; p->id.sport = 40;

        if (dport == CI_DTP_DATA_PORT) {
            uint32_t off = (uint32_t)i * step;
            p->id.flags = 0;                       /* DTP: no RDP bit */
            p->data[0] = (uint8_t)(off & 0xFF);
            p->data[1] = (uint8_t)((off >> 8) & 0xFF);
            p->data[2] = (uint8_t)((off >> 16) & 0xFF);
            p->data[3] = (uint8_t)((off >> 24) & 0xFF);
            p->data[4] = 0xDE; p->data[5] = 0xAD; p->data[6] = 0xBE; p->data[7] = 0xEF;
        } else {
            uint16_t seq = (uint16_t)i;
            p->id.flags = CI_CSP_FRDP;             /* RDP marker */
            p->data[0] = 0xDE; p->data[1] = 0xAD; p->data[2] = 0xBE;
            p->data[3] = CI_RDP_ACK;
            p->data[4] = (uint8_t)(seq >> 8);
            p->data[5] = (uint8_t)(seq & 0xFF);
            p->data[6] = 0; p->data[7] = 0;
        }
        p->length = 8;

        /* TX through the shim: it drops+frees a reproducible subset and delegates the
         * rest to the CAN iface. Either way the buffer is consumed, so get a fresh one. */
        shim.iface.nexthop(&shim.iface, dst, p, 1);
        p = NULL;
        usleep(2000);   /* pace TX so the slow CAN/monitor keep up */
    }

    usleep(300000);
    if (drop_log) { fflush(drop_log); fclose(drop_log); }
    printf("ci_inject_can: sent %d on %s (dport %d), injected_drops=%llu forwarded=%llu\n",
           count, dev, dport, (unsigned long long)shim.injected_drops,
           (unsigned long long)shim.forwarded);
    return 0;
}
