/*
 * svu_server.c - the satellite side of the Self-Verifying Uploader.
 *
 * Serves a file over CSP as the corrected DTP: on a reliable meta request it
 * replies with the per-block SHA-256 manifest, then blasts the requested byte
 * ranges connectionless (fire-and-forget) as [offset][session][payload] packets.
 * A resume request just carries the ranges the client still needs.
 *
 * SCAFFOLD STATUS: compiles and follows the real libcsp idiom, but is not yet
 * exercised on the flatsat. TODOs mark the pieces the flatsat bring-up must add
 * (pacing to the link rate, client liveness/keep-alive, multi-session). The
 * integrity/manifest logic is the proven lib/ci_svu core, reused verbatim.
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

static uint8_t *load_file(const char *path, uint32_t *size_out)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long n = ftell(fp);
    if (n < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    uint8_t *buf = malloc((size_t)n);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size_out = (uint32_t)n;
    return buf;
}

/* Send the manifest (nblocks * 32 bytes) over the reliable conn in chunks. */
static void send_manifest(csp_conn_t *conn, const uint8_t *manifest, uint32_t bytes)
{
    const uint32_t chunk = 200u;
    for (uint32_t off = 0u; off < bytes; off += chunk) {
        uint32_t len = (off + chunk <= bytes) ? chunk : (bytes - off);
        csp_packet_t *pkt = csp_buffer_get(0);
        if (pkt == NULL) {
            return;
        }
        memcpy(pkt->data, manifest + off, len);
        pkt->length = (uint16_t)len;
        csp_send(conn, pkt);
    }
}

/* Blast one byte interval [start,end) as fire-and-forget data packets. */
static void blast_interval(uint16_t client_addr, uint32_t session, uint32_t mtu,
                           const uint8_t *src, uint32_t start, uint32_t end)
{
    uint32_t pay = (mtu > SVU_DATA_HDR) ? (mtu - SVU_DATA_HDR) : 1u;
    for (uint32_t off = start; off < end; off += pay) {
        uint32_t len = (off + pay <= end) ? pay : (end - off);
        csp_packet_t *pkt = csp_buffer_get(0);
        if (pkt == NULL) {
            return;
        }
        svu_put32(pkt->data + 0, off);
        svu_put32(pkt->data + 4, session);
        memcpy(pkt->data + SVU_DATA_HDR, src + off, len);
        pkt->length = (uint16_t)(SVU_DATA_HDR + len);
        /* fire-and-forget: no RDP, no CRC option -- the corrected DTP data path */
        csp_sendto(CSP_PRIO_NORM, client_addr, SVU_DATA_PORT, SVU_DATA_PORT, 0, pkt);
        /* TODO(flatsat): pace to the link rate here (usleep per packet). */
    }
}

int main(int argc, char **argv)
{
    const char *can_dev = "can0";
    const char *file = NULL;
    uint16_t addr = 5;
    uint32_t block_size = 4096u;
    int bitrate = 0; /* 0 = do NOT reconfigure a live bus (default, safe) */

    int opt;
    while ((opt = getopt(argc, argv, "c:a:f:b:B:h")) != -1) {
        switch (opt) {
        case 'c': can_dev = optarg; break;
        case 'a': addr = (uint16_t)atoi(optarg); break;
        case 'f': file = optarg; break;
        case 'b': block_size = (uint32_t)strtoul(optarg, NULL, 10); break;
        case 'B': bitrate = atoi(optarg); break;
        case 'h':
        default:
            printf("usage: %s -f <file> [-c can0] [-a addr] [-b block_size] "
                   "[-B bitrate(0=leave bus as-is)]\n", argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }
    if (file == NULL || block_size == 0u) {
        printf("error: -f <file> is required and -b block_size must be > 0\n");
        return 1;
    }

    uint32_t total = 0u;
    uint8_t *src = load_file(file, &total);
    if (src == NULL || total == 0u) {
        printf("error: cannot read '%s'\n", file);
        return 1;
    }
    uint32_t nblocks = ci_svu_nblocks(total, block_size);
    uint8_t *manifest = malloc((size_t)nblocks * CI_SVU_HASH_LEN);
    if (manifest == NULL) {
        return 1;
    }
    ci_svu_manifest(src, total, block_size, manifest);

    if (svu_net_init(can_dev, addr, bitrate) != 0) {
        return 1;
    }
    csp_print("svu-server: serving '%s' (%u bytes, %u blocks) on node %u\n",
              file, total, nblocks, addr);

    csp_socket_t ctrl = {0};
    csp_bind(&ctrl, SVU_CTRL_PORT);
    csp_listen(&ctrl, 10);

    while (1) {
        csp_conn_t *conn = csp_accept(&ctrl, 10000);
        if (conn == NULL) {
            continue;
        }
        csp_packet_t *pkt = csp_read(conn, 5000);
        if (pkt == NULL) {
            csp_close(conn);
            continue;
        }
        svu_req_t req;
        if (svu_req_decode(pkt->data, pkt->length, &req) != 0) {
            csp_buffer_free(pkt);
            csp_close(conn);
            continue;
        }
        csp_buffer_free(pkt);

        uint32_t mtu = (req.mtu > SVU_DATA_HDR) ? req.mtu : 256u;
        uint16_t client_addr = (uint16_t)req.client_addr;

        /* meta response header + manifest, over the reliable conn */
        csp_packet_t *resp = csp_buffer_get(0);
        if (resp != NULL) {
            svu_put32(resp->data + 0, SVU_MAGIC_RESP);
            svu_put32(resp->data + 4, req.session_id);
            svu_put32(resp->data + 8, total);
            svu_put32(resp->data + 12, block_size);
            svu_put32(resp->data + 16, nblocks);
            resp->length = (uint16_t)SVU_RESP_HDR;
            csp_send(conn, resp);
            send_manifest(conn, manifest, nblocks * CI_SVU_HASH_LEN);
        }
        csp_close(conn);

        /* blast: 0 intervals means "the whole file" (initial request) */
        if (req.nof_intervals == 0u) {
            blast_interval(client_addr, req.session_id, mtu, src, 0u, total);
        } else {
            for (uint32_t i = 0u; i < req.nof_intervals; i++) {
                uint32_t s = req.intervals[i].start;
                uint32_t e = req.intervals[i].end;
                if (e > total) {
                    e = total;
                }
                if (s < e) {
                    blast_interval(client_addr, req.session_id, mtu, src, s, e);
                }
            }
        }
        csp_print("svu-server: served a request from node %u (%u intervals)\n",
                  client_addr, req.nof_intervals);
    }

    return 0;
}
