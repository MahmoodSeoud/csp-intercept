/*
 * ci_dtp.h - parse DTP bulk-data packets (the DTP side of the RDP-vs-DTP study).
 *
 * DTP uses separate CSP ports:
 *   port 7  = control handshake (reliable / RDP)
 *   port 8  = bulk data, connectionless and unreliable (no RDP)
 *   port 13 = a separate metadata RPC (reliable / RDP)
 *
 * DTP bulk-data wire format, as observed on port 8:
 *   data[0..3] = uint32 little-endian byte OFFSET into the payload
 *   data[4..]  = payload bytes
 * Logical fragment index = offset / (mtu - 4). `mtu` is per-session (negotiated
 * on the control channel); useful payload per packet = mtu - 4. A full data
 * packet has length == mtu; the final packet is shorter.
 *
 * Only the on-the-wire format this parser handles is documented here. Protocol
 * behaviour and the RDP-vs-DTP analysis live in the (local) design doc.
 */
#ifndef CI_DTP_H
#define CI_DTP_H

#include <stddef.h>
#include <stdint.h>

#define CI_DTP_CONTROL_PORT 7   /* RDP meta handshake            */
#define CI_DTP_DATA_PORT    8   /* connectionless bulk data      */
#define CI_DIPP_META_PORT   13  /* DIPP ring_size/observation RPC (RDP), ground side */
#define CI_DTP_OFFSET_SIZE  4   /* leading uint32 LE byte-offset header */

/*
 * Parse the leading 4-byte little-endian byte-offset of a DTP data packet.
 * Returns 0 on success, -1 if data is NULL or len < CI_DTP_OFFSET_SIZE.
 */
int ci_dtp_parse_offset(const uint8_t *data, size_t len, uint32_t *offset_out);

/*
 * Recover the logical fragment index from a byte offset.
 * Returns 0 on success, -1 if mtu <= CI_DTP_OFFSET_SIZE (need >4 for useful payload).
 */
int ci_dtp_fragment_index(uint32_t offset, uint16_t mtu, uint32_t *frag_out);

#endif /* CI_DTP_H */
