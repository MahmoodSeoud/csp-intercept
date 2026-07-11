/*
 * svu_session.h - the SVU client transfer loop, decoupled from CSP bring-up.
 *
 * The whole point: this runs on an ALREADY-INITIALIZED CSP stack. A standalone
 * binary calls svu_net_init() first; the CSH APM calls nothing (csh already did
 * csp_init + the CAN interface + routes). Same transfer logic either way, so the
 * developer's csh session -- bus up, node map known -- is reused instead of
 * re-created. That reuse is the entire DX win of the APM form factor.
 */
#ifndef SVU_SESSION_H
#define SVU_SESSION_H

#include <stdint.h>

/*
 * Pull a file from `server_addr` over SVU, verify it block-by-block, and write it
 * to `outfile`. Assumes CSP is already initialized and the interface is up. Returns
 * 0 on a block-verified transfer, -1 otherwise. `block_size`/`mtu` are the client's
 * requested values; the server's manifest is authoritative for block size.
 */
int svu_client_run(uint16_t server_addr, uint32_t block_size, uint32_t mtu,
                   uint32_t max_rounds, const char *outfile);

#endif /* SVU_SESSION_H */
