/*
 * svu_daemon.c - the SVU receiver daemon. The CSP equivalent of sshd: it runs on the
 * destination node and is always listening, so nobody has to start a receiver by hand.
 *
 * A source contacts it on SVU_PUSH_PORT and hands over a destination path; the daemon
 * pulls the file from that source (svu_client_run, reusing the verified transfer loop),
 * writes it to the path, and replies OK/FAIL. Persistent: it frees the receive port
 * after each push (csp_socket_close inside svu_client_run) and loops for the next one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <getopt.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>

#include "svu_net.h"
#include "svu_session.h"
#include "svu_proto.h"

int main(int argc, char **argv)
{
    const char *can_dev = "can0";
    const char *zmq_host = NULL;
    uint16_t addr = 30;
    int bitrate = 0;

    int opt;
    while ((opt = getopt(argc, argv, "c:z:a:B:h")) != -1) {
        switch (opt) {
        case 'c': can_dev = optarg; break;
        case 'z': zmq_host = optarg; break;
        case 'a': addr = (uint16_t)atoi(optarg); break;
        case 'B': bitrate = atoi(optarg); break;
        case 'h':
        default:
            printf("usage: %s [-c can0 | -z broker_host] [-a addr] [-B bitrate]\n"
                   "  SVU receiver daemon. Run on the destination node; a source pushes\n"
                   "  with `svu <file> <this-addr>:<dest-path>`.\n", argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    int net_err = (zmq_host != NULL) ? svu_net_init_zmq(zmq_host, addr)
                                     : svu_net_init(can_dev, addr, bitrate);
    if (net_err != 0) {
        return 1;
    }

    csp_socket_t sock = {0};
    if (csp_bind(&sock, SVU_PUSH_PORT) != CSP_ERR_NONE) {
        printf("svu-daemon: cannot bind push port %u\n", SVU_PUSH_PORT);
        return 1;
    }
    csp_listen(&sock, 10);
    csp_print("svu-daemon: listening on node %u (push port %u)\n", addr, SVU_PUSH_PORT);

    while (1) {
        csp_conn_t *conn = csp_accept(&sock, 10000);
        if (conn == NULL) {
            continue;
        }
        csp_packet_t *pkt = csp_read(conn, 5000);
        if (pkt == NULL) {
            csp_close(conn);
            continue;
        }
        /* announce payload = [magic][u32 mode][dest path], or legacy path-only.
         * mode = the source's permission bits when the client used -p (0 otherwise). */
        char path[256];
        uint32_t mode = 0u;
        svu_announce_decode(pkt->data, pkt->length, &mode, path, sizeof(path));
        uint16_t src = (uint16_t)csp_conn_src(conn);
        csp_buffer_free(pkt);

        csp_print("svu-daemon: push from node %u -> '%s'\n", src, path);
        int rc = svu_client_run(src, 4096u, 256u, 500u, path);
        if (rc == 0 && mode != 0u) {
            /* preserve mode (svu -p): apply the source's bits to the written file */
            if (chmod(path, (mode_t)mode) != 0) {
                csp_print("svu-daemon: warning: chmod('%s', 0%o) failed\n", path, mode);
            }
        }

        /* reply on the same conn: 1 status byte (0 = VERIFIED, 1 = failed). */
        csp_packet_t *resp = csp_buffer_get(0);
        if (resp != NULL) {
            resp->data[0] = (rc == 0) ? 0u : 1u;
            resp->length = 1u;
            csp_send(conn, resp);
        }
        csp_close(conn);
        csp_print("svu-daemon: '%s' %s\n", path, (rc == 0) ? "VERIFIED" : "FAILED");
    }
    return 0;
}
