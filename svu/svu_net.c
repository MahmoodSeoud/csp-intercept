/*
 * svu_net.c - CSP + SocketCAN bring-up, mirroring the libcsp posix examples
 * (examples/csp_server.c add_interface + examples/csp_server_client_posix.c
 * router task). Kept deliberately small; the interesting code is the protocol.
 */
#include "svu_net.h"

#include <pthread.h>

#include <stdio.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>
#include <csp/drivers/can_socketcan.h>
#include <csp/interfaces/csp_if_zmqhub.h>

static void *task_router(void *param)
{
    (void)param;
    while (1) {
        csp_route_work();
    }
    return NULL;
}

/* Make this node answer CSP services (ping, ident, ...) so it behaves like any real
 * node — `ping <addr>` is the natural liveness check for a persistent svu_daemon.
 * CSP_ANY is a fallback: connection-oriented binds (SVU ctrl/data/push ports) still
 * take precedence for their own ports. */
static void bind_services(void)
{
    csp_bind_callback(csp_service_handler, CSP_ANY);
}

/* Mark the interface default and spawn the detached router thread. Shared by both
 * the CAN and ZMQ bring-up paths. Returns 0 on success, -1 on failure. */
static int start_router(csp_iface_t *iface)
{
    iface->is_default = 1;

    pthread_attr_t attr;
    pthread_t handle;
    if (pthread_attr_init(&attr) != 0) {
        return -1;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int ret = pthread_create(&handle, &attr, task_router, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        csp_print("svu: failed to start router thread (%d)\n", ret);
        return -1;
    }
    return 0;
}

int svu_net_init(const char *can_dev, uint16_t addr, int bitrate)
{
    csp_init();
    bind_services();

    /* bitrate <= 0 -> pass 0 so libcsp leaves the (already-up) interface alone. */
    int br = (bitrate > 0) ? bitrate : 0;
    csp_iface_t *iface = NULL;
    int err = csp_can_socketcan_open_and_add_interface(
        can_dev, CSP_IF_CAN_DEFAULT_NAME, addr, br, true, &iface);
    if (err != CSP_ERR_NONE || iface == NULL) {
        csp_print("svu: failed to add CAN interface [%s], error %d\n", can_dev, err);
        return -1;
    }
    return start_router(iface);
}

int svu_net_init_zmq(const char *host, uint16_t addr)
{
    csp_init();
    bind_services();

    /* publish (tx) -> broker subscribe port 6000; subscribe (rx) -> broker publish
     * port 7000. Same convention as upload_gs-server -z and the injector's zmq side. */
    char pub_ep[64];
    char sub_ep[64];
    snprintf(pub_ep, sizeof(pub_ep), "tcp://%s:6000", host);
    snprintf(sub_ep, sizeof(sub_ep), "tcp://%s:7000", host);

    csp_iface_t *iface = NULL;
    int err = csp_zmqhub_init_w_endpoints(addr, pub_ep, sub_ep, 0, &iface);
    if (err != CSP_ERR_NONE || iface == NULL) {
        csp_print("svu: failed to add ZMQ interface [%s], error %d\n", host, err);
        return -1;
    }
    return start_router(iface);
}
