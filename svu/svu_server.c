/*
 * svu_server.c - the standalone SVU sender binary. Brings up its own CSP stack
 * (svu_net_init) then runs the shared serve loop (svu_serve.c). The serve logic
 * is shared verbatim with the CSH `svu_put` APM.
 *
 * SCAFFOLD STATUS: compiles and follows the real libcsp idiom. TODOs the flatsat
 * bring-up must add live in svu_serve.c (pacing, client liveness, multi-session).
 */
#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>

#include "svu_net.h"
#include "svu_serve.h"

int main(int argc, char **argv)
{
    const char *can_dev = "can0";
    const char *zmq_host = NULL; /* -z <host>: use zmq broker instead of CAN */
    const char *file = NULL;
    uint16_t addr = 5;
    uint32_t block_size = 4096u;
    int bitrate = 0; /* 0 = do NOT reconfigure a live bus (default, safe) */

    int opt;
    while ((opt = getopt(argc, argv, "c:z:a:f:b:B:h")) != -1) {
        switch (opt) {
        case 'c': can_dev = optarg; break;
        case 'z': zmq_host = optarg; break;
        case 'a': addr = (uint16_t)atoi(optarg); break;
        case 'f': file = optarg; break;
        case 'b': block_size = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'B': bitrate = atoi(optarg); break;
        case 'h':
        default:
            printf("usage: %s -f <file> [-c can0 | -z broker_host] [-a addr] "
                   "[-b block_size] [-B bitrate(0=leave bus as-is)]\n"
                   "  -c <dev>   join a SocketCAN bus (default: can0)\n"
                   "  -z <host>  join a zmqproxy broker at <host> instead (for the\n"
                   "             loss injector: publish->6000, subscribe->7000)\n", argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }
    if (file == NULL || block_size == 0u) {
        printf("error: -f <file> is required and -b block_size must be > 0\n");
        return 1;
    }

    uint32_t total = 0u;
    uint8_t *src = svu_load_file(file, &total);
    if (src == NULL || total == 0u) {
        printf("error: cannot read '%s'\n", file);
        return 1;
    }

    int net_err = (zmq_host != NULL)
                      ? svu_net_init_zmq(zmq_host, addr)
                      : svu_net_init(can_dev, addr, bitrate);
    if (net_err != 0) {
        return 1;
    }
    csp_print("svu-server: serving '%s' (%u bytes) on node %u via %s\n",
              file, total, addr, (zmq_host != NULL) ? zmq_host : can_dev);

    return svu_serve_loop(src, total, block_size, NULL); /* NULL = serve forever */
}
