/*
 * svu_serve.c - SVU sender/serve loop (see svu_serve.h). Extracted verbatim from
 * svu_server.c's main so the standalone binary and the CSH `svu_put` APM share one
 * implementation. Does NOT call csp_init / add an interface -- the caller owns the
 * CSP stack.
 */
#include "svu_serve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <csp/csp.h>
#include <csp/csp_debug.h>

#include "svu_proto.h"
#include "ci_svu.h"

uint8_t *svu_load_file(const char *path, uint32_t *size_out)
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
    }
}

int svu_serve_loop(const uint8_t *src, uint32_t total, uint32_t block_size,
                   volatile int *stop)
{
    uint32_t nblocks = ci_svu_nblocks(total, block_size);
    uint8_t *manifest = malloc((size_t)nblocks * CI_SVU_HASH_LEN);
    if (manifest == NULL) {
        return -1;
    }
    ci_svu_manifest(src, total, block_size, manifest);

    csp_socket_t ctrl = {0};
    if (csp_bind(&ctrl, SVU_CTRL_PORT) != CSP_ERR_NONE) {
        free(manifest);
        return -1;
    }
    csp_listen(&ctrl, 10);

    while (1) {
        csp_conn_t *conn = csp_accept(&ctrl, 2000);
        if (conn == NULL) {
            if (stop != NULL && *stop != 0) {
                break;
            }
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
        /* Learn the client's address from the CONNECTION, not a self-reported field. */
        uint16_t client_addr = (uint16_t)csp_conn_src(conn);

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

    csp_socket_close(&ctrl); /* free the port so this file (or the next) can be re-served */
    free(manifest);
    return 0;
}
