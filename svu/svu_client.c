/*
 * svu_client.c - standalone ground-side SVU client. Thin wrapper: parse args, bring
 * up CSP + CAN, then run the shared transfer loop (svu_session.c). For interactive
 * use inside a csh session, prefer the `svu_get` slash command (svu_apm.c), which
 * runs the same loop on csh's already-live CSP stack.
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "svu_net.h"
#include "svu_session.h"

static void usage(const char *argv0)
{
    printf("usage: %s -C <server-node> [options]\n", argv0);
    printf("  -C <addr>   server (sender) CSP node address   [REQUIRED]\n");
    printf("  -c <dev>    CAN device                         [can0]\n");
    printf("  -z <host>   join a zmqproxy broker instead of CAN (loopback tests)\n");
    printf("  -a <addr>   my CSP node address                [10]\n");
    printf("  -o <file>   output file                        [svu_out.bin]\n");
    printf("  -b <n>      block size (bytes)                 [4096]\n");
    printf("  -m <n>      MTU (bytes)                        [256]\n");
    printf("  -B <n>      CAN bitrate (0 = leave bus as-is)  [0]\n");
    printf("\nexample: %s -C 25 -o image.bin        # pull from node 25 on can0\n", argv0);
}

int main(int argc, char **argv)
{
    const char *can_dev = "can0";
    const char *zmq_host = NULL; /* -z <host>: use zmq broker instead of CAN */
    const char *outfile = "svu_out.bin";
    uint16_t addr = 10;
    uint16_t server_addr = 0;
    int have_server = 0;
    uint32_t mtu = 256u;
    uint32_t block_size = 4096u;
    int bitrate = 0; /* 0 = do NOT reconfigure a live bus (default, safe) */

    int opt;
    while ((opt = getopt(argc, argv, "c:z:a:C:o:m:b:B:h")) != -1) {
        switch (opt) {
        case 'c': can_dev = optarg; break;
        case 'z': zmq_host = optarg; break;
        case 'a': addr = (uint16_t)atoi(optarg); break;
        case 'C': server_addr = (uint16_t)atoi(optarg); have_server = 1; break;
        case 'o': outfile = optarg; break;
        case 'm': mtu = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'b': block_size = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'B': bitrate = atoi(optarg); break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (!have_server) {
        fprintf(stderr, "error: -C <server-node> is required "
                        "(the CSP address of the sender).\n\n");
        usage(argv[0]);
        return 1;
    }

    int net_err = (zmq_host != NULL)
                      ? svu_net_init_zmq(zmq_host, addr)
                      : svu_net_init(can_dev, addr, bitrate);
    if (net_err != 0) {
        return 1;
    }
    return (svu_client_run(server_addr, block_size, mtu, 500u, outfile) == 0) ? 0 : 1;
}
