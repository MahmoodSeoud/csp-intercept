#include "ci_drop_iface.h"

#include <string.h>

#include <csp/csp_buffer.h>
#include <csp/arch/csp_time.h>

#include "ci_rdp.h"

int ci_drop_iface_nexthop(csp_iface_t *iface, uint16_t via,
                          csp_packet_t *packet, int from_me) {
    ci_drop_iface_t *s = (ci_drop_iface_t *)iface->driver_data;

    /* Parse the same protocol view the proxy/APM build from the wire. At nexthop the
     * packet still carries its CSP id and the payload in data[0..length); the RDP
     * trailer (if any) is the last 5 bytes, exactly as ci_frame_from_fields expects. */
    ci_frame_t f;
    ci_frame_from_fields(packet->id.dport, packet->id.flags,
                         packet->data, packet->length, &f);

    /* Out of scope -> forward untouched (no drop, not logged: the proxy doesn't log
     * out-of-scope frames either, so the oracle stays comparable). */
    if (!ci_rule_match(&s->rule, &f)) {
        s->passthrough++;
        return s->target->nexthop(s->target, via, packet, from_me);
    }

    uint64_t idx  = ci_flow_index(&s->tracker, &f, packet->data, packet->length);
    int      drop = ci_rule_decide(&s->rule, idx);

    if (s->drop_log) {
        /* Schema identical to the proxy drop-log: t_ms,src,dport,csp_flags,is_rdp,
         * index,epoch,dropped -- so the same oracle_join / two_oracle tooling works. */
        fprintf(s->drop_log, "%u,%u,%u,0x%02X,%d,%llu,%u,%d\n",
                csp_get_ms(), packet->id.src, packet->id.dport, packet->id.flags,
                f.is_rdp, (unsigned long long)idx, s->tracker.epoch, drop);
        fflush(s->drop_log);
    }

    if (drop) {
        /* TODOS#2 drop contract: free + CSP_ERR_NONE (NOT an error code, which would
         * double-free and inflate tx_error). Count on our own injected-drop counter. */
        s->injected_drops++;
        csp_buffer_free(packet);
        return CSP_ERR_NONE;
    }

    s->forwarded++;
    return s->target->nexthop(s->target, via, packet, from_me);
}

int ci_drop_iface_init(ci_drop_iface_t *s, const char *name, csp_iface_t *target,
                       const ci_drop_rule_t *rule, uint16_t dtp_mtu, FILE *drop_log) {
    if (s == NULL || name == NULL || target == NULL || rule == NULL) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    s->target   = target;
    s->rule     = *rule;
    s->drop_log = drop_log;
    ci_flow_tracker_init(&s->tracker, dtp_mtu);

    s->iface.name        = name;
    s->iface.addr        = target->addr;
    s->iface.netmask     = target->netmask;
    s->iface.nexthop     = ci_drop_iface_nexthop;
    s->iface.driver_data = s;
    return 0;
}
