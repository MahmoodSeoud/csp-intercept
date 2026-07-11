/*
 * svu_net.h - shared CSP bring-up for the SVU client and server binaries.
 *
 * Both ends need the same thing: init CSP, join the CAN bus with an address, and
 * start a router task. This is the flatsat-facing plumbing kept out of the two
 * protocol files so they read as protocol, not boilerplate.
 */
#ifndef SVU_NET_H
#define SVU_NET_H

#include <stdint.h>

/*
 * Initialize CSP, open the SocketCAN device `can_dev` (e.g. "can0") with local
 * address `addr`, mark it default, and spawn the router thread. Returns 0 on
 * success, -1 on failure (message printed via csp_print).
 *
 * `bitrate` is passed straight to libcsp: 0 means "use the interface as-is, do NOT
 * reconfigure it" -- REQUIRED when joining an already-up shared bus (the flatsat
 * can0), because any positive value makes libcsp can_set_bitrate + restart the
 * device and disrupt every other node. Pass a positive value only to bring up a
 * bus you own.
 */
int svu_net_init(const char *can_dev, uint16_t addr, int bitrate);

#endif /* SVU_NET_H */
