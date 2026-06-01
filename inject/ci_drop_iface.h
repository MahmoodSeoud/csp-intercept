/*
 * ci_drop_iface.h - in-path deterministic drop shim (CAN / KISS / any nexthop).
 *
 * The lossy zmqproxy can only sit in a virtual ZMQ bus. A real UHF/CAN/KISS link has
 * no broker to insert loss into: the fault must be injected in-path, on a node's own
 * transmit, before the frame reaches the wire. This shim is a csp_iface_t whose
 * nexthop applies the SAME deterministic, CSP-aware drop rule as the proxy (keyed by
 * per-flow protocol identity via the shared ci_flow_* tracker), then delegates kept
 * frames to a real downstream interface (e.g. the CAN iface).
 *
 * Drop contract (TODOS#2, verified against libcsp csp_send_direct): on a drop we
 *   csp_buffer_free(packet); return CSP_ERR_NONE;
 * and bump our OWN injected-drop counter. We must NOT return an error code: libcsp
 * treats a nexthop error as a TX failure, which double-frees the packet and pollutes
 * iface->tx_error -- corrupting the very loss measurement we are taking.
 *
 * Reproducibility: identical seed + identical per-flow identities => identical drop
 * set as the proxy, so the proxy drop-log and this shim are interchangeable oracle A.
 */
#ifndef CI_DROP_IFACE_H
#define CI_DROP_IFACE_H

#include <stdint.h>
#include <stdio.h>

#include <csp/csp.h>
#include <csp/csp_interface.h>

#include "ci_rule.h"

typedef struct {
    csp_iface_t        iface;     /* the shim interface registered with csp        */
    csp_iface_t *      target;    /* downstream real interface (CAN/KISS/...)       */
    ci_drop_rule_t     rule;      /* match scope + seed/probability (or replay)     */
    ci_flow_tracker_t  tracker;   /* per-flow identity (RDP wrap epoch / DTP mtu)   */
    FILE *             drop_log;  /* optional oracle CSV (NULL = no log)            */
    uint64_t           injected_drops;  /* our own counter (NOT iface->drop/tx_err) */
    uint64_t           forwarded;       /* in-scope frames delegated downstream     */
    uint64_t           passthrough;     /* out-of-scope frames delegated untouched  */
} ci_drop_iface_t;

/*
 * Initialise the shim around an existing, already-registered `target` interface.
 * `name` is the shim's csp iface name (<= CSP_IFLIST_NAME_MAX). `rule` is copied.
 * `dtp_mtu` seeds the flow tracker (match the proxy / client). `drop_log` may be NULL.
 * Does NOT register the shim with csp_iflist; the caller routes to &s->iface as needed.
 * Returns 0 on success, -1 on bad args.
 */
int ci_drop_iface_init(ci_drop_iface_t *s, const char *name, csp_iface_t *target,
                       const ci_drop_rule_t *rule, uint16_t dtp_mtu, FILE *drop_log);

/* The nexthop installed on s->iface. Exposed for direct unit testing. */
int ci_drop_iface_nexthop(csp_iface_t *iface, uint16_t via,
                          csp_packet_t *packet, int from_me);

#endif /* CI_DROP_IFACE_H */
