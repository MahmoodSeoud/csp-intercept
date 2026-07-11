/*
 * svu_net.c - CSP + SocketCAN bring-up, mirroring the libcsp posix examples
 * (examples/csp_server.c add_interface + examples/csp_server_client_posix.c
 * router task). Kept deliberately small; the interesting code is the protocol.
 */
#include "svu_net.h"

#include <pthread.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>
#include <csp/drivers/can_socketcan.h>

static void *task_router(void *param)
{
    (void)param;
    while (1) {
        csp_route_work();
    }
    return NULL;
}

int svu_net_init(const char *can_dev, uint16_t addr, int bitrate)
{
    csp_init();

    /* bitrate <= 0 -> pass 0 so libcsp leaves the (already-up) interface alone. */
    int br = (bitrate > 0) ? bitrate : 0;
    csp_iface_t *iface = NULL;
    int err = csp_can_socketcan_open_and_add_interface(
        can_dev, CSP_IF_CAN_DEFAULT_NAME, addr, br, true, &iface);
    if (err != CSP_ERR_NONE || iface == NULL) {
        csp_print("svu: failed to add CAN interface [%s], error %d\n", can_dev, err);
        return -1;
    }
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
