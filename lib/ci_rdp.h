/*
 * ci_rdp.h - parse libcsp's RDP header for the monitor and the dropper.
 *
 * Source provenance (verified against the public spaceinventor/libcsp,
 * src/csp_rdp.c -- these constants are private to csp_rdp.c, not in any public
 * header, so we duplicate + pin them; a libcsp refactor could change them):
 *
 *   - RDP header is a 5-byte PACKED big-endian TRAILER at the END of the packet
 *     payload, i.e. the last 5 bytes counted by packet->length, NOT a prefix.
 *       struct __packed { uint8_t flags; uint16_t seq_nr; uint16_t ack_nr; }
 *     (csp_rdp.c:43-47; appended by csp_rdp_header_add at :56-65 which does
 *      packet->length += 5; read back by csp_rdp_header_ref at data[length-5].)
 *   - seq_nr/ack_nr are big-endian on the wire (htobe16 on TX, be16toh on RX:
 *     csp_rdp.c:140-141, 440-441).
 *   - Flag bits (csp_rdp.c:25-28): SYN=0x08 ACK=0x04 EAK=0x02 RST=0x01.
 *   - The flags BYTE's high nibble is an anti-dedup counter, so classify on
 *     (flags & 0x0F) -- testing `flags == RDP_SYN` raw is WRONG. (Eng review RDP-04.)
 *   - A packet is RDP iff (csp id.flags & CSP_FRDP), CSP_FRDP=0x02
 *     (libcsp include/csp/csp_types.h:72). NOTE: CSP_FHMAC is also 0x08
 *     but lives in id.flags, a DIFFERENT field from the trailer flags -- do not
 *     confuse the two. (Eng review AF-05.)
 *
 * libcsp pin: spaceinventor/libcsp (record the exact submodule commit when this
 * repo vendors libcsp).
 */
#ifndef CI_RDP_H
#define CI_RDP_H

#include <stddef.h>
#include <stdint.h>

#define CI_RDP_HEADER_SIZE 5    /* matches CSP_RDP_HEADER_SIZE (csp_types.h:168) */

/* RDP trailer flag bits (private to csp_rdp.c; pinned). */
#define CI_RDP_SYN 0x08
#define CI_RDP_ACK 0x04
#define CI_RDP_EAK 0x02
#define CI_RDP_RST 0x01
#define CI_RDP_FLAG_MASK 0x0F   /* high nibble = anti-dedup counter; mask it off */

/* csp id.flags bit marking an RDP packet (csp_types.h:72). */
#define CI_CSP_FRDP 0x02

typedef struct {
    uint8_t  flags;   /* already masked with CI_RDP_FLAG_MASK */
    uint16_t seq;     /* host order, decoded from big-endian */
    uint16_t ack;     /* host order */
} ci_rdp_header_t;

/*
 * Parse the 5-byte big-endian RDP trailer from the LAST 5 bytes of [data, len].
 * Returns 0 on success, -1 if data is NULL or len < CI_RDP_HEADER_SIZE.
 * This does NOT decide whether the packet is RDP -- the caller must gate on
 * (csp id.flags & CI_CSP_FRDP) first; raw payload bytes are otherwise
 * indistinguishable from a trailer.
 */
int ci_rdp_parse_trailer(const uint8_t *data, size_t len, ci_rdp_header_t *out);

#endif /* CI_RDP_H */
