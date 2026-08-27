// components/platform/platform_esp32/src/ble_internal.h —— 平台内 BLE 协作接口
#pragma once
#include <stdint.h>

// 由 ble_config.c 的 on_sync 调用:在广播启动后启动 BLE 扫描(observer)。
// addr_type 为 ble_hs_id_infer_auto 推得的本机地址类型。
void ble_scan_start(uint8_t addr_type);
