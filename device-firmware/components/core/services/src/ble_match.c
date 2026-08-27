// components/core/services/src/ble_match.c
#include "services/ble_match.h"

ble_match_kind_t ble_match_classify(const uint8_t *mfg, int len) {
    if (mfg != NULL && len == 26 && mfg[0] == 'H' && mfg[1] == 'B') {
        if (mfg[2] == BLE_MATCH_HDR_PROFILE) return BLE_MATCH_PROFILE;
        if (mfg[2] == BLE_MATCH_HDR_PROFILE_COMMON ||
            mfg[2] == BLE_MATCH_HDR_PROFILE_NICKNAME) return BLE_MATCH_PROFILE_BATCH;
    }
    if (mfg == NULL || len < BLE_MATCH_PREFIX_LEN) return BLE_MATCH_NONE;
    if (!(mfg[0] == BLE_MATCH_PREFIX_0 && mfg[1] == BLE_MATCH_PREFIX_1 &&
          mfg[2] == BLE_MATCH_PREFIX_2 && mfg[3] == BLE_MATCH_PREFIX_3))
        return BLE_MATCH_NONE;
    // 先判 token 再回落心跳:保证 nRF Connect 手动发的 4 字节 FFFF4842 仍是心跳,
    // 将来加第三类消息只需占用一个新的 hdr 值。
    if (len >= BLE_MATCH_PREFIX_LEN + 1 && mfg[4] == BLE_MATCH_HDR_TOKEN)
        return BLE_MATCH_TOKEN;
    return BLE_MATCH_HEARTBEAT;
}

bool ble_match_is_heartbeat(const uint8_t *mfg, int len) {
    return ble_match_classify(mfg, len) == BLE_MATCH_HEARTBEAT;
}
