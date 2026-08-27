// components/platform_esp32/src/platform_sys.c —— 平台系统级服务(与外设编译开关无关)
#include "platform/platform_factory.h"
#include "esp_random.h"

// 硬件 RNG:每次开机产出不同的值。通用工具,当前无调用方。
uint32_t platform_random_seed(void) {
    return esp_random();
}
