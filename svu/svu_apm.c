/*
 * svu_apm.c - CSH APM: the Self-Verifying Uploader as a slash command.
 *
 * `svu_get <server-node> [-o FILE]` pulls a file over SVU and block-verifies it,
 * reusing the SAME transfer loop as the standalone binary (svu_session.c). The
 * point of the APM form factor: it runs on the CSP stack csh ALREADY brought up
 * (csp_init, the CAN interface, routes, the node map) -- so from a live csh on the
 * flatsat the developer types one line instead of leaving csh to run a separate
 * binary that re-initializes its own node. Meet developers where they are.
 *
 * Load it like any DISCO APM (the loader picks up libcsh_*.so). The server side
 * stays a deployable binary (the satellite is headless); this is the ground UX.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <apm/apm.h>
#include <slash/slash.h>
#include <slash/optparse.h>

#include "svu_session.h"

static int svu_get_cmd(struct slash *slash)
{
    char *out = "svu_out.bin";
    int block = 4096;
    int mtu = 256;

    optparse_t *p = optparse_new("svu_get", "<server-node>");
    optparse_add_help(p);
    optparse_add_string(p, 'o', "out", "FILE", &out, "output file (default svu_out.bin)");
    optparse_add_int(p, 'b', "block", "BYTES", 10, &block, "block size (default 4096)");
    optparse_add_int(p, 'm', "mtu", "BYTES", 10, &mtu, "MTU (default 256)");
    int argi = optparse_parse(p, slash->argc - 1, (const char **)slash->argv + 1);
    if (argi < 0) {
        optparse_del(p);
        return SLASH_EINVAL;
    }
    /* optparse parsed argv+1, so the first positional is argv[1 + argi]. */
    if (1 + argi >= slash->argc) {
        printf("svu_get: missing <server-node>\n");
        printf("  usage: svu_get <server-node> [-o FILE] [-b BLOCK] [-m MTU]\n");
        optparse_del(p);
        return SLASH_EINVAL;
    }
    uint16_t server = (uint16_t)atoi(slash->argv[1 + argi]);
    optparse_del(p);

    if (block <= 0 || mtu <= (int)0) {
        printf("svu_get: block and mtu must be positive\n");
        return SLASH_EINVAL;
    }

    printf("svu_get: pulling from node %u -> %s (block %d, mtu %d)\n",
           server, out, block, mtu);
    int rc = svu_client_run(server, (uint32_t)block, (uint32_t)mtu, 500u, out);
    return (rc == 0) ? SLASH_SUCCESS : SLASH_EIO;
}
slash_command(svu_get, svu_get_cmd, "<server-node> [-o FILE] [-b BLOCK] [-m MTU]",
              "Pull a file over the Self-Verifying Uploader and block-verify it");

int apm_init(void)
{
    return 0;
}
