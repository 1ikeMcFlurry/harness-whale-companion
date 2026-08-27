// components/core/ports/include/hal/hal_battery.h —— 电池读取端口(电量计 SOC / 或电压)
#pragma once

typedef struct hal_battery_s hal_battery_t;
typedef struct {
    // 电量计直接给出的荷电量百分比 0..100;-1=读失败或该实现不支持(如纯 ADC)。
    int (*read_soc)(hal_battery_t *self);
    // 电池电压 mV(ADC 实现按分压还原;电量计实现读 VCELL);<0=失败/不支持。
    int (*read_mv)(hal_battery_t *self);
} hal_battery_api_t;
struct hal_battery_s { const hal_battery_api_t *api; void *impl; };

static inline int hal_battery_read_soc(hal_battery_t *b) {
    return (b && b->api && b->api->read_soc) ? b->api->read_soc(b) : -1;
}
static inline int hal_battery_read_mv(hal_battery_t *b) {
    return (b && b->api && b->api->read_mv) ? b->api->read_mv(b) : -1;
}
