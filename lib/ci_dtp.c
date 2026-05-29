#include "ci_dtp.h"

int ci_dtp_parse_offset(const uint8_t *data, size_t len, uint32_t *offset_out) {
    if (data == NULL || offset_out == NULL || len < CI_DTP_OFFSET_SIZE) {
        return -1;
    }
    /* little-endian byte offset (low byte first) */
    *offset_out = (uint32_t)data[0]
                | ((uint32_t)data[1] << 8)
                | ((uint32_t)data[2] << 16)
                | ((uint32_t)data[3] << 24);
    return 0;
}

int ci_dtp_fragment_index(uint32_t offset, uint16_t mtu, uint32_t *frag_out) {
    if (frag_out == NULL || mtu <= CI_DTP_OFFSET_SIZE) {
        return -1;
    }
    *frag_out = offset / (uint32_t)(mtu - CI_DTP_OFFSET_SIZE);
    return 0;
}
