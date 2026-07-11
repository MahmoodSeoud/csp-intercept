/*
 * svu_client.c - the ground side of the Self-Verifying Uploader.
 *
 * Requests a file, receives the manifest, takes the fire-and-forget blast into the
 * proven lib/ci_svu tracker, and loops: verify -> re-request exactly the missing or
 * corrupt byte ranges -> repeat until the reassembled file is block-verified, then
 * write it out. This is the host resume-loop (tests/svu_loop_host.c) with a CSP
 * transport instead of an in-process channel; the brain is identical.
 *
 * SCAFFOLD STATUS: compiles and follows the real libcsp idiom, not yet run on the
 * flatsat. TODOs mark flatsat bring-up (idle-window tuning, >SVU_MAX_INTERVALS
 * splitting, keep-alive). Integrity/resume logic is lib/ci_svu, reused verbatim.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>

#include "svu_net.h"
#include "svu_proto.h"
#include "ci_svu.h"

/* Receive data packets until the link goes idle for `idle_ms`, feeding ci_svu. */
static void drain_data(csp_socket_t *data_sock, ci_svu_t *recv, uint32_t idle_ms)
{
    csp_packet_t *pkt;
    while ((pkt = csp_recvfrom(data_sock, idle_ms)) != NULL) {
        if (pkt->length > SVU_DATA_HDR) {
            uint32_t off = svu_get32(pkt->data + 0);
            uint32_t len = (uint32_t)pkt->length - SVU_DATA_HDR;
            ci_svu_accept(recv, off, pkt->data + SVU_DATA_HDR, len);
        }
        csp_buffer_free(pkt);
    }
}

/* One CTRL round: send the request, read the resp header + manifest. Returns 0 on
 * success and fills total/block/nblocks + the manifest buffer (malloc'd by caller
 * once nblocks is known via *nblocks_out). */
static int ctrl_exchange(uint16_t server_addr, const svu_req_t *req,
                         uint32_t *total_out, uint32_t *block_out,
                         uint32_t *nblocks_out, uint8_t **manifest_out)
{
    uint8_t buf[SVU_REQ_HDR + (SVU_MAX_INTERVALS * 8u)];
    size_t nbytes = svu_req_encode(req, buf);

    /* Plain CSP connection (no RDP handshake): the first packet we send opens the
     * connection on the server's csp_accept. CRC32 still guards the small control
     * messages. Bulk data stays fire-and-forget on the data port. */
    csp_conn_t *conn = csp_connect(CSP_PRIO_NORM, server_addr, SVU_CTRL_PORT,
                                   10000, CSP_O_CRC32);
    if (conn == NULL) {
        return -1;
    }
    csp_packet_t *qp = csp_buffer_get(0);
    if (qp == NULL) {
        csp_close(conn);
        return -1;
    }
    memcpy(qp->data, buf, nbytes);
    qp->length = (uint16_t)nbytes;
    csp_send(conn, qp);

    /* resp header first */
    csp_packet_t *hp = csp_read(conn, 10000);
    if (hp == NULL || hp->length < SVU_RESP_HDR ||
        svu_get32(hp->data + 0) != SVU_MAGIC_RESP) {
        if (hp != NULL) {
            csp_buffer_free(hp);
        }
        csp_close(conn);
        return -1;
    }
    uint32_t total = svu_get32(hp->data + 8);
    uint32_t block = svu_get32(hp->data + 12);
    uint32_t nblocks = svu_get32(hp->data + 16);
    csp_buffer_free(hp);

    uint32_t man_bytes = nblocks * CI_SVU_HASH_LEN;
    uint8_t *manifest = malloc(man_bytes);
    if (manifest == NULL) {
        csp_close(conn);
        return -1;
    }
    uint32_t got = 0u;
    while (got < man_bytes) {
        csp_packet_t *mp = csp_read(conn, 10000);
        if (mp == NULL) {
            free(manifest);
            csp_close(conn);
            return -1;
        }
        uint32_t take = mp->length;
        if (got + take > man_bytes) {
            take = man_bytes - got;
        }
        memcpy(manifest + got, mp->data, take);
        got += take;
        csp_buffer_free(mp);
    }
    csp_close(conn);

    *total_out = total;
    *block_out = block;
    *nblocks_out = nblocks;
    *manifest_out = manifest;
    return 0;
}

int main(int argc, char **argv)
{
    const char *can_dev = "can0";
    const char *outfile = "svu_out.bin";
    uint16_t addr = 10;
    uint16_t server_addr = 5;
    uint32_t mtu = 256u;
    uint32_t block_size = 4096u;
    uint32_t max_rounds = 500u;
    int bitrate = 0; /* 0 = do NOT reconfigure a live bus (default, safe) */

    int opt;
    while ((opt = getopt(argc, argv, "c:a:C:o:m:b:B:h")) != -1) {
        switch (opt) {
        case 'c': can_dev = optarg; break;
        case 'a': addr = (uint16_t)atoi(optarg); break;
        case 'C': server_addr = (uint16_t)atoi(optarg); break;
        case 'o': outfile = optarg; break;
        case 'm': mtu = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'b': block_size = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'B': bitrate = atoi(optarg); break;
        case 'h':
        default:
            printf("usage: %s -C <server> [-c can0] [-a addr] [-o out] "
                   "[-m mtu] [-b block] [-B bitrate(0=leave bus as-is)]\n", argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    if (svu_net_init(can_dev, addr, bitrate) != 0) {
        return 1;
    }
    /* Connectionless RX socket for the fire-and-forget data blast. CSP requires
     * CSP_SO_CONN_LESS *and* csp_listen() (which allocates the rx_queue -- csp_bind
     * alone does not). Without both, data packets fall into the connection path and
     * the router dereferences a NULL queue. */
    csp_socket_t data_sock = {0};
    data_sock.opts = CSP_SO_CONN_LESS;
    csp_bind(&data_sock, SVU_DATA_PORT);
    csp_listen(&data_sock, 10);

    ci_svu_t *recv = NULL;
    uint32_t total = 0u, block = 0u, nblocks = 0u;
    ci_svu_interval_t ivs[SVU_MAX_INTERVALS];
    uint32_t nreq = 0u; /* 0 on the first round => request the whole file */

    ci_svu_status_t st = CI_SVU_INCOMPLETE;
    uint32_t round = 0u;
    while (round < max_rounds) {
        round++;

        svu_req_t req;
        memset(&req, 0, sizeof(req));
        req.magic = SVU_MAGIC_REQ;
        req.session_id = 1u;
        req.mtu = mtu;
        req.block_size = block_size;
        req.client_addr = addr;
        req.nof_intervals = nreq;
        for (uint32_t i = 0u; i < nreq; i++) {
            req.intervals[i].start = ivs[i].start;
            req.intervals[i].end = ivs[i].end;
        }

        uint8_t *manifest = NULL;
        uint32_t t2 = 0u, b2 = 0u, nb2 = 0u;
        if (ctrl_exchange(server_addr, &req, &t2, &b2, &nb2, &manifest) != 0) {
            csp_print("svu-client: ctrl exchange failed (round %u)\n", round);
            return 1;
        }
        if (recv == NULL) {
            total = t2;
            block = b2;
            nblocks = nb2;
            recv = ci_svu_new(total, block, manifest, nblocks);
            if (recv == NULL) {
                csp_print("svu-client: ci_svu_new failed\n");
                free(manifest);
                return 1;
            }
        }
        free(manifest);

        /* TODO(flatsat): tune the idle window to the pass/link timing. */
        drain_data(&data_sock, recv, 2000);

        uint32_t nout = 0u;
        st = ci_svu_verify(recv, ivs, SVU_MAX_INTERVALS, &nout);
        if (st == CI_SVU_COMPLETE_VERIFIED) {
            break;
        }
        /* TODO(flatsat): if nout > SVU_MAX_INTERVALS, carry the remainder into
         * the next round instead of dropping it. */
        nreq = (nout < SVU_MAX_INTERVALS) ? nout : SVU_MAX_INTERVALS;
        csp_print("svu-client: round %u -> %s, re-requesting %u range(s)\n",
                  round, (st == CI_SVU_CORRUPT) ? "CORRUPT" : "INCOMPLETE", nreq);
    }

    if (st != CI_SVU_COMPLETE_VERIFIED) {
        csp_print("svu-client: gave up after %u rounds (status %d)\n", round, st);
        return 1;
    }

    FILE *fp = fopen(outfile, "wb");
    if (fp == NULL) {
        csp_print("svu-client: cannot open '%s' for writing\n", outfile);
        return 1;
    }
    fwrite(ci_svu_data(recv), 1, total, fp);
    fclose(fp);
    csp_print("svu-client: VERIFIED %u bytes in %u round(s) -> %s\n",
              total, round, outfile);
    ci_svu_free(recv);
    return 0;
}
