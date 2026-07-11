/*
 * svu_debug.c - opt-in TX/RX tracing via libcsp's weak packet hooks. Set
 * SVU_DEBUG=1 in the environment to print every packet the stack sends/receives,
 * so bring-up can see exactly where a packet dies on the wire. No-op otherwise.
 */
#include <stdio.h>
#include <stdlib.h>

#include <csp/csp.h>
#include <csp/csp_hooks.h>

static int svu_dbg_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("SVU_DEBUG");
        v = (e != NULL && e[0] != '\0') ? 1 : 0;
    }
    return v;
}

void csp_input_hook(csp_iface_t *iface, csp_packet_t *packet)
{
    if (svu_dbg_on()) {
        fprintf(stderr, "[RX] if=%s dst=%u src=%u dport=%u sport=%u len=%u\n",
                (iface != NULL && iface->name != NULL) ? iface->name : "?",
                packet->id.dst, packet->id.src, packet->id.dport,
                packet->id.sport, packet->length);
    }
}

void csp_output_hook(csp_id_t *idout, csp_packet_t *packet, csp_iface_t *iface,
                     uint16_t via, int from_me)
{
    (void)via;
    (void)from_me;
    if (svu_dbg_on()) {
        fprintf(stderr, "[TX] if=%s dst=%u src=%u dport=%u sport=%u len=%u\n",
                (iface != NULL && iface->name != NULL) ? iface->name : "?",
                idout->dst, idout->src, idout->dport, idout->sport,
                packet->length);
    }
}
