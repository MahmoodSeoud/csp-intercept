/*
 * vmem_node - a minimal CSP node that serves a big, flat RAM vmem region on a CAN bus.
 *
 * Purpose: a clean, byte-faithful integrity oracle for the RDP / CSH-`upload` arm. Unlike
 * DIPP's `stora` (10 KB, file-backed, wedges under retransmit stress) or a csh instance
 * (which does not serve vmem at all), this exposes a large VMEM_TYPE_RAM region that
 * round-trips bytes exactly and does not overflow. Upload a file to it over the injector,
 * read it back with download/crc32, compare against the original.
 *
 * In-project (this repo) — no vendor / disco/src edits. Built by tests/e2e/meson.build.
 *
 * Usage:  vmem_node -c can0 -a 5431            (default: can0, addr 5431, 1 MiB region)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>

#include <string.h>
#include <csp/csp.h>
#include <csp/drivers/can_socketcan.h>
#include <vmem/vmem.h>
#include <vmem/vmem_server.h>

/* 1 MiB flat RAM scratch: fits the pinned 256 KiB payload and larger, byte-faithful,
 * cannot wedge like a file/NVMe-backed region.
 *
 * Defined with custom read/write over a static buffer and a FIXED vaddr, instead of the
 * VMEM_DEFINE_STATIC_RAM macro. The macro sets vaddr = &heap (an ASLR-dynamic pointer that
 * moves each launch), which forces callers to discover the address. A fixed vaddr means
 * experiments/exp_rdp.csh can hardcode the address and be a runnable, reproducible .csh.
 * The server passes (client_addr - vaddr) as the offset to write()/read(), so vaddr is a
 * logical base only (never dereferenced) — safe to pin. */
#define BIGMEM_SIZE  1048576
#define BIGMEM_VADDR 0x10000000ULL      /* hardcode this in exp_rdp.csh */

static uint8_t bigmem_buf[BIGMEM_SIZE];

static void bigmem_read(vmem_t *vmem, uint64_t offset, void *dst, uint32_t len) {
    (void)vmem;
    if (offset <= BIGMEM_SIZE && offset + len <= BIGMEM_SIZE) memcpy(dst, bigmem_buf + offset, len);
}
static void bigmem_write(vmem_t *vmem, uint64_t offset, const void *src, uint32_t len) {
    (void)vmem;
    if (offset <= BIGMEM_SIZE && offset + len <= BIGMEM_SIZE) memcpy(bigmem_buf + offset, src, len);
}

__attribute__((section("vmem"), aligned(__alignof__(vmem_t)), used))
vmem_t vmem_bigmem = {
    .type = VMEM_TYPE_RAM,
    .read = bigmem_read,
    .write = bigmem_write,
    .flush = NULL,
    .vaddr = BIGMEM_VADDR,
    .size = BIGMEM_SIZE,
    .name = "bigmem",
    .big_endian = 0,
    .ack_with_pull = 1,
    .driver = NULL,
};

static void *router_task(void *param) { (void)param; while (1) { csp_route_work(); } return NULL; }
static void *vmem_task(void *param)   { vmem_server_loop(param); return NULL; }

int main(int argc, char **argv)
{
    const char *dev = "can0";
    unsigned int addr = 5431;
    int opt;
    while ((opt = getopt(argc, argv, "c:a:h")) != -1) {
        switch (opt) {
        case 'c': dev = optarg; break;
        case 'a': addr = (unsigned int)atoi(optarg); break;
        case 'h':
        default:
            printf("usage: %s -c <can-device> -a <address>\n", argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    csp_init();

    csp_iface_t *iface = NULL;
    int err = csp_can_socketcan_open_and_add_interface(dev, "CAN", addr, 1000000, true, &iface);
    if (err != CSP_ERR_NONE) {
        fprintf(stderr, "vmem_node: failed to open CAN [%s], error %d\n", dev, err);
        return 1;
    }
    iface->is_default = 1;

    /* services (ping/ident) so health checks work; vmem_server binds its own port */
    csp_bind_callback(csp_service_handler, CSP_ANY);

    pthread_t rt, vt;
    pthread_create(&rt, NULL, router_task, NULL);
    pthread_create(&vt, NULL, vmem_task, NULL);

    printf("vmem_node up: addr=%u dev=%s region=bigmem size=1048576\n", addr, dev);
    fflush(stdout);

    while (1) { sleep(3600); }
    return 0;
}
