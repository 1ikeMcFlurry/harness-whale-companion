// components/core/services/src/sn_format.c
#include "services/sn_format.h"

static const char HEX[] = "0123456789abcdef";

void sn_from_mac(const uint8_t mac[6], char out[SN_STR_LEN + 1]) {
    for (int i = 0; i < 6; i++) {
        out[i * 2]     = HEX[(mac[i] >> 4) & 0x0F];
        out[i * 2 + 1] = HEX[mac[i] & 0x0F];
    }
    out[SN_STR_LEN] = '\0';
}
