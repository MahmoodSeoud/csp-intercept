/*
 * ci_monitor_host - the live "APM observing the bus" end of the two-oracle loop.
 *
 * Unlike apm_host.c (which injects promisc clones synthetically to unit-test the
 * drain/leak contract), this is a REAL libcsp node that joins the lossy proxy over
 * a zmqhub interface, exactly as a csh node would. It loads the actual csp_monitor
 * APM (by #including its TU) and lets the genuine zmqhub RX thread + router clone
 * every forwarded frame into the promiscuous queue, which the APM drainer reads and
 * writes to a frozen-schema CSV. That CSV is oracle #2 (observed) for the join
 * against the proxy's drop-log (oracle #1, dropped).
 *
 * This replaces upload_gs-server + dtp_client + csh for the MVP "whoa": any CSP
 * traffic through the proxy proves the two oracles partition the input. ci_gen
 * supplies a deterministic RDP stream (port 13); the proxy drops a reproducible
 * subset by per-flow identity (RDP seq); the monitor must observe exactly the rest.
 *
 * Connection: csp_zmqhub_init(addr, host, 0, &iface) connects SUB->proxy backend
 * (CSP_ZMQPROXY_PUBLISH_PORT 7000) and PUB->proxy frontend
 * (CSP_ZMQPROXY_SUBSCRIBE_PORT 6000), subscribing to ALL frames (no addr filter --
 * csp_if_zmqhub.c:197), so we see ci_gen's dst=20 traffic even though our addr != 20.
 *
 * Usage: ci_monitor_host <host> <addr> <dport> <out.csv> <run_ms>
 *   defaults: localhost 19 13 apm.csv 4000
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
#include <csp/interfaces/csp_if_zmqhub.h>

/* Pull the real APM into this TU: the static start/stop command fns, the drainer,
 * and the module-static monitor state become drivable from here -- the exact same
 * code path a csh `apm load` + `csp_monitor start` would exercise. */
#include "csp_monitor_apm.c"

extern csp_conf_t csp_conf;

/* Drive a real slash command: the command fns only read slash->argc / slash->argv. */
static int run_cmd(int (*fn)(struct slash *), int argc, char **argv) {
    struct slash s;
    memset(&s, 0, sizeof(s));
    s.argc = argc;
    s.argv = argv;
    return fn(&s);
}

int main(int argc, char **argv) {
    const char *host    = (argc > 1) ? argv[1] : "127.0.0.1";
    int         frontp  = (argc > 2) ? atoi(argv[2]) : 6000;   /* proxy frontend (XSUB): we PUB here  */
    int         backp   = (argc > 3) ? atoi(argv[3]) : 7000;   /* proxy backend  (XPUB): we SUB here  */
    uint16_t    addr    = (uint16_t)((argc > 4) ? atoi(argv[4]) : 19);
    const char *dport   = (argc > 5) ? argv[5] : "13";
    char *      out     = (argc > 6) ? argv[6] : "apm.csv";
    int         run_ms  = (argc > 7) ? atoi(argv[7]) : 4000;

    csp_conf.version = 2;
    csp_init();

    /* Join the lossy proxy exactly as a real node does, but with EXPLICIT endpoints
     * so the harness can pick non-default ports (csp_zmqhub_init hardcodes 6000/7000,
     * which collide when meson runs E2E tests in parallel). The publisher endpoint is
     * the proxy frontend (XSUB); the subscriber endpoint is the backend (XPUB). flags=0:
     * not interface-promiscuous, but csp_zmqhub subscribes to ALL frames (no ZMQ addr
     * filter, csp_if_zmqhub.c:197), and csp_promisc_enable (inside the APM) is what makes
     * the router clone every routed packet into the queue we drain. */
    char pub_ep[64], sub_ep[64];
    snprintf(pub_ep, sizeof(pub_ep), "tcp://%s:%d", host, frontp);
    snprintf(sub_ep, sizeof(sub_ep), "tcp://%s:%d", host, backp);
    csp_iface_t *iface = NULL;
    int rc = csp_zmqhub_init_w_endpoints(addr, pub_ep, sub_ep, 0, &iface);
    if (rc != CSP_ERR_NONE || iface == NULL) {
        fprintf(stderr, "ci_monitor_host: csp_zmqhub_init_w_endpoints(%s/%d,%d) failed rc=%d\n",
                host, frontp, backp, rc);
        return 1;
    }
    /* Route everything we receive to ourselves so the router runs the promisc clone
     * path for every frame regardless of dst (we are a tap, not the addressee). */
    csp_rtable_set(0, 0, iface, CSP_NO_VIA_ADDRESS);

    /* Give the ZMQ SUB connection + subscription time to establish before traffic. */
    usleep(500000);

    char *start_argv[] = {"start", "-o", out, "-d", (char *)dport, "-w", "1000"};
    rc = run_cmd(csp_monitor_start_cmd, 7, start_argv);
    if (rc != SLASH_SUCCESS) {
        fprintf(stderr, "ci_monitor_host: csp_monitor start rc=%d\n", rc);
        return 1;
    }
    printf("ci_monitor_host: monitoring %s (pub %d / sub %d) addr=%u dport=%s -> %s for %dms\n",
           host, frontp, backp, addr, dport, out, run_ms);
    fflush(stdout);

    /* The zmqhub RX thread writes received frames into the qfifo; the router must run
     * to drain it, and it is csp_route_work() that clones each routed packet into the
     * promiscuous queue the APM drainer reads. libcsp does not auto-start a router
     * task here (csp.h exposes only csp_route_work), so pump it ourselves for the run.
     * csp_route_work() returns promptly when the qfifo is empty, so bound the pump on
     * wall-clock (csp_get_ms) rather than a loop count -- correct even if a future
     * qfifo read grows an internal timeout. */
    uint32_t t_end = csp_get_ms() + (uint32_t)run_ms;
    while (csp_get_ms() < t_end) {
        csp_route_work();
        usleep(2000);
    }

    char *stop_argv[] = {"stop"};
    run_cmd(csp_monitor_stop_cmd, 1, stop_argv);
    printf("ci_monitor_host: done -> %s\n", out);
    return 0;
}
