/*
 * svu_apm.c - CSH APM: the scp-like SVU uploader as a slash command.
 *
 *     svu <src-file> <node>:<dest-path>
 *
 * Pushes a local file onto a node's filesystem over SVU, block-verified -- the scp
 * experience, from a live csh. The destination node runs `svu_daemon` (the sshd of
 * this scheme); here we serve the file (svu_serve_loop) on the csh stack that's
 * already up, hand the daemon the dest path over SVU_PUSH_PORT, and print its verdict.
 * If it can't be verified it says FAILED -- it never reports success on wrong bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include <apm/apm.h>
#include <apm/csh_api.h>   /* get_host_by_addr_or_name: resolve <node> by name or number */
#include <slash/slash.h>
#include <slash/optparse.h>
#include <csp/csp.h>

#include "svu_serve.h"
#include "svu_proto.h"

/* csh's global default node (what every command's -n falls back to; set with `node <addr>`).
 * Exported by csh; resolved at APM dlopen. Lets `svu <src> <dst>` skip the node, like cp. */
extern unsigned int slash_dfl_node;

struct svu_serve_arg {
    const uint8_t *src;
    uint32_t total;
    uint32_t block;
    volatile int *stop;
};

static void *svu_serve_thr(void *arg)
{
    struct svu_serve_arg *s = (struct svu_serve_arg *)arg;
    (void)svu_serve_loop(s->src, s->total, s->block, s->stop);
    return NULL;
}

static int svu_cmd(struct slash *slash)
{
    int block = 4096;
    int preserve = 0;

    optparse_t *p = optparse_new("svu", "<src-file> [<node>:]<dest-path>");
    optparse_add_help(p);
    optparse_add_int(p, 'b', "block", "BYTES", 10, &block, "block size (default 4096)");
    optparse_add_set(p, 'p', "preserve", 1, &preserve, "preserve source mode (like scp -p)");
    int argi = optparse_parse(p, slash->argc - 1, (const char **)slash->argv + 1);
    if (argi < 0) {
        optparse_del(p);
        return SLASH_EINVAL;
    }
    /* optparse parsed argv+1, so the first positional is argv[1 + argi]. */
    int base = 1 + argi;
    if (base + 1 >= slash->argc) {
        printf("usage: svu <src-file> [<node>:]<dest-path>   (no node = default, set with `node <addr>`)\n");
        printf("  e.g. svu image.bin /home/root/image.bin   OR   svu image.bin 5426:/home/root/image.bin\n");
        optparse_del(p);
        return SLASH_EINVAL;
    }
    const char *srcfile = slash->argv[base];
    const char *target = slash->argv[base + 1];
    optparse_del(p);

    if (block <= 0) {
        printf("svu: block must be positive\n");
        return SLASH_EINVAL;
    }

    /* Target is scp-style "<node>:<path>", or -- when no node is given -- just "<path>",
     * which uploads to the default node (set once with `node <addr>`), like `cp`. A colon
     * separates the node only when it precedes the first '/', so absolute dest paths with
     * no node ("/home/root/x") are unambiguous. */
    const char *colon = strchr(target, ':');
    const char *firstslash = strchr(target, '/');
    const char *destpath;
    uint16_t node;
    if (colon != NULL && (firstslash == NULL || colon < firstslash)) {
        char nodebuf[64]; /* names can be longer than a number */
        size_t nlen = (size_t)(colon - target);
        if (nlen == 0u || nlen >= sizeof(nodebuf)) {
            printf("svu: bad node in target '%s'\n", target);
            return SLASH_EINVAL;
        }
        memcpy(nodebuf, target, nlen);
        nodebuf[nlen] = '\0';
        /* resolve <node> as a known host NAME or a CSP address (like scp's host aliases) */
        int node_addr = 0;
        if (get_host_by_addr_or_name(&node_addr, nodebuf) <= 0) {
            printf("svu: unknown node '%s' (use a CSP address or a known host name; see `node`)\n", nodebuf);
            return SLASH_EINVAL;
        }
        node = (uint16_t)node_addr;
        destpath = colon + 1;
    } else {
        /* no node -> default node, whole target is the dest path (the cp-like form) */
        if (slash_dfl_node == 0u) {
            printf("svu: no destination node set. Either set a default once with `node <addr>`\n"
                   "     (e.g. `node 5426`) then `svu <src> <dest-path>`, or name it explicitly:\n"
                   "     `svu <src> <node>:<dest-path>`.\n");
            return SLASH_EINVAL;
        }
        node = (uint16_t)slash_dfl_node;
        destpath = target;
    }
    if (destpath[0] == '\0') {
        printf("svu: empty dest path\n");
        return SLASH_EINVAL;
    }

    uint32_t total = 0u;
    uint8_t *src = svu_load_file(srcfile, &total);
    if (src == NULL || total == 0u) {
        printf("svu: cannot read '%s'\n", srcfile);
        return SLASH_EIO;
    }

    /* -p: capture the source file's permission bits to hand to the daemon */
    uint32_t mode = 0u;
    if (preserve) {
        struct stat st;
        if (stat(srcfile, &st) == 0) {
            mode = (uint32_t)(st.st_mode & 07777);
        }
    }

    /* serve the file (stoppable) so the daemon can pull it, verified */
    volatile int stop = 0;
    struct svu_serve_arg sarg = { src, total, (uint32_t)block, &stop };
    pthread_t th;
    if (pthread_create(&th, NULL, svu_serve_thr, &sarg) != 0) {
        printf("svu: failed to start serve thread\n");
        free(src);
        return SLASH_EIO;
    }
    usleep(300000); /* let the serve loop bind SVU_CTRL_PORT before we announce */

    /* announce: hand the daemon the dest path; it pulls from us and verifies */
    int ok = 0;
    csp_conn_t *conn = csp_connect(CSP_PRIO_NORM, node, SVU_PUSH_PORT, 10000, CSP_O_CRC32);
    if (conn == NULL) {
        printf("svu: cannot reach svu_daemon on node %u (is it running?)\n", node);
    } else {
        csp_packet_t *ap = csp_buffer_get(0);
        if (ap != NULL) {
            ap->length = (uint16_t)svu_announce_encode(mode, destpath, ap->data, 250u);
            csp_send(conn, ap);

            csp_packet_t *rp = csp_read(conn, 600000);
            ok = (rp != NULL && rp->length >= 1u && rp->data[0] == 0u);
            if (rp != NULL) {
                csp_buffer_free(rp);
            }
        }
        csp_close(conn);
    }

    /* stop serving and reclaim the CTRL port so `svu` can be run again */
    stop = 1;
    pthread_join(th, NULL);
    free(src);

    if (ok) {
        printf("svu: %s -> %u:%s  VERIFIED (%u bytes)\n", srcfile, node, destpath, total);
        return SLASH_SUCCESS;
    }
    printf("svu: %s -> %u:%s  FAILED\n", srcfile, node, destpath);
    return SLASH_EIO;
}
slash_command(svu, svu_cmd, "<src-file> [<node>:]<dest-path>",
              "scp-like upload over SVU; the destination node runs svu_daemon");

int apm_init(void)
{
    return 0;
}
