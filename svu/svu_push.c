/*
 * svu_push.c - `svu`: scp-like one-command upload over SVU. Pushes a local file onto
 * a node's filesystem, block-verified. Syntax mirrors scp:
 *
 *     svu <src-file> <node>:<dest-path>
 *
 * The far node must be running svu_daemon. Under the hood we serve the file
 * (svu_serve_loop) and hand the daemon the dest path over SVU_PUSH_PORT; the daemon
 * pulls, block-verifies, and writes it, then replies with the verdict. If it can't be
 * verified it says so -- it never reports success on wrong bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <getopt.h>
#include <pthread.h>
#include <unistd.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>

#include "svu_net.h"
#include "svu_serve.h"
#include "svu_proto.h"

struct serve_ctx {
    const uint8_t *src;
    uint32_t total;
    uint32_t block;
};

static void *serve_thread(void *arg)
{
    struct serve_ctx *c = (struct serve_ctx *)arg;
    (void)svu_serve_loop(c->src, c->total, c->block, NULL); /* serves until the process exits */
    return NULL;
}

int main(int argc, char **argv)
{
    const char *can_dev = "can0";
    const char *zmq_host = NULL;
    uint16_t my_addr = 31;
    int bitrate = 0;
    uint32_t block = 4096u;
    int preserve = 0;

    int opt;
    while ((opt = getopt(argc, argv, "c:z:a:b:B:ph")) != -1) {
        switch (opt) {
        case 'c': can_dev = optarg; break;
        case 'z': zmq_host = optarg; break;
        case 'a': my_addr = (uint16_t)atoi(optarg); break;
        case 'b': block = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'B': bitrate = atoi(optarg); break;
        case 'p': preserve = 1; break;
        case 'h':
        default:
            printf("usage: %s [-a my_addr] [-p] [-c can0 | -z host] <src-file> <node>:<dest-path>\n"
                   "  scp-like upload over SVU; the far node runs svu_daemon.\n"
                   "  -p  preserve the source file's mode (like scp -p), e.g. keep +x\n"
                   "  e.g. svu -p vmem_node 5426:/home/root/vmem_node\n", argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }
    if (optind + 2 > argc) {
        printf("usage: %s [-a my_addr] <src-file> <node>:<dest-path>\n", argv[0]);
        return 1;
    }
    const char *srcfile = argv[optind];
    const char *target = argv[optind + 1]; /* node:path */

    const char *colon = strchr(target, ':');
    if (colon == NULL) {
        printf("svu: target must be <node>:<dest-path> (e.g. 30:/home/root/x.bin)\n");
        return 1;
    }
    char nodebuf[16];
    size_t nlen = (size_t)(colon - target);
    if (nlen == 0u || nlen >= sizeof(nodebuf)) {
        printf("svu: bad node in target '%s'\n", target);
        return 1;
    }
    memcpy(nodebuf, target, nlen);
    nodebuf[nlen] = '\0';
    uint16_t node = (uint16_t)atoi(nodebuf);
    const char *destpath = colon + 1;
    if (destpath[0] == '\0') {
        printf("svu: empty dest path\n");
        return 1;
    }

    uint32_t total = 0u;
    uint8_t *src = svu_load_file(srcfile, &total);
    if (src == NULL || total == 0u) {
        printf("svu: cannot read '%s'\n", srcfile);
        return 1;
    }

    /* -p: capture the source file's permission bits to hand to the daemon */
    uint32_t mode = 0u;
    if (preserve) {
        struct stat st;
        if (stat(srcfile, &st) == 0) {
            mode = (uint32_t)(st.st_mode & 07777);
        }
    }

    int net_err = (zmq_host != NULL) ? svu_net_init_zmq(zmq_host, my_addr)
                                     : svu_net_init(can_dev, my_addr, bitrate);
    if (net_err != 0) {
        return 1;
    }

    /* serve the file so the daemon can pull it (verified) */
    static struct serve_ctx ctx;
    ctx.src = src;
    ctx.total = total;
    ctx.block = block;
    pthread_attr_t attr;
    pthread_t th;
    if (pthread_attr_init(&attr) != 0) {
        return 1;
    }
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int trc = pthread_create(&th, &attr, serve_thread, &ctx);
    pthread_attr_destroy(&attr);
    if (trc != 0) {
        printf("svu: failed to start serve thread (%d)\n", trc);
        return 1;
    }
    usleep(300000); /* let the serve loop bind SVU_CTRL_PORT before we announce */

    /* announce: hand the daemon the dest path; it pulls from us and verifies */
    csp_conn_t *conn = csp_connect(CSP_PRIO_NORM, node, SVU_PUSH_PORT, 10000, CSP_O_CRC32);
    if (conn == NULL) {
        printf("svu: cannot reach svu_daemon on node %u (is it running?)\n", node);
        return 1;
    }
    csp_packet_t *ap = csp_buffer_get(0);
    if (ap == NULL) {
        csp_close(conn);
        return 1;
    }
    ap->length = (uint16_t)svu_announce_encode(mode, destpath, ap->data, 250u);
    csp_send(conn, ap);

    /* wait for the daemon's verdict (it pulls + verifies first, so allow time) */
    csp_packet_t *rp = csp_read(conn, 600000);
    int ok = (rp != NULL && rp->length >= 1u && rp->data[0] == 0u);
    if (rp != NULL) {
        csp_buffer_free(rp);
    }
    csp_close(conn);

    if (ok) {
        printf("svu: %s -> %u:%s  VERIFIED (%u bytes)\n", srcfile, node, destpath, total);
        return 0;
    }
    printf("svu: %s -> %u:%s  FAILED\n", srcfile, node, destpath);
    return 1;
}
