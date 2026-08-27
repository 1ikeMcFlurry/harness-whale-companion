// components/platform/platform_esp32/src/battery_cw2017.c —— CW2017 锂电电量计驱动(hal_battery)
// CellWise CW2017,I2C 7 位地址 0x63,与 ES8311 共用 I2C_NUM_0 总线。
// 电量计自带 FastCali,直接给 SOC 百分比(0x04 高字节即整数%),无需采样电阻/电压查表。
// 默认关闭时(PERIPH_BATTERY=0)本文件整段空编译;启用后总线上无芯片则探测失败返回 NULL,
// 上层退回虚拟电量,不崩。电池 profile 见 cw2017_profiles.h(可切换),写入流程见下。
#include "platform/board_config.h"
#if PERIPH_BATTERY
#include "platform/platform_factory.h"
#include "hal/hal_battery.h"
#include "cw2017_profiles.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cw2017";

#define CW_ADDR           0x63   // 7 位从地址(读 0xC7 / 写 0xC6)
#define CW_REG_VERSION    0x00   // 默认 0xA0
#define CW_REG_VCELL_H    0x02   // 14bit,V(uV)=raw*312.5
#define CW_REG_SOC_H      0x04   // 高字节=整数百分比,低字节(0x05)=1/256%
#define CW_REG_CONFIG     0x08   // 0xF0=睡眠;0x30=复位态;0x00=正常。0x30→0x00 重载 profile
#define CW_REG_SOC_ALERT  0x0B   // bit7=UPDATE_FLAG(profile 已写标记);[6:0]=SOC 报警阈值
#define CW_REG_VOLT_ID_H  0x0E   // ID 引脚 ADC(有符号 14bit),V(uV)=raw*312.5
#define CW_REG_BATINFO    0x10   // profile 区 0x10..0x5F(80 字节,CW221x/2017 家族 BATINFO)
#define CW_CONFIG_RESTART 0x30
#define CW_CONFIG_NORMAL  0x00

#ifndef CW2017_PROFILE_SEL
#define CW2017_PROFILE_SEL 0     // 选用哪个电池 profile(索引进 CW2017_PROFILES[])
#endif
#ifndef CW2017_WRITE_PROFILE
#define CW2017_WRITE_PROFILE 1   // 1=按选定 profile 写入芯片;0=用芯片自带 Li-Poly profile
#endif

static i2c_master_dev_handle_t s_dev;

static int cw_rd(uint8_t reg, uint8_t *buf, size_t n) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100) == ESP_OK ? 0 : -1;
}
static int cw_wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK ? 0 : -1;
}

// SOC:0x04 高字节直接是整数百分比。>100(如 0xFF)=尚未估算完/异常 → 返回 -1 让上层维持旧值。
static int cw_read_soc(hal_battery_t *self) {
    (void)self;
    uint8_t b[2];
    if (cw_rd(CW_REG_SOC_H, b, 2) != 0) return -1;
    if (b[0] > 100) return -1;
    return b[0];
}
// VCELL:14bit,mV = raw*312.5/1000 = raw*5/16。
static int cw_read_mv(hal_battery_t *self) {
    (void)self;
    uint8_t b[2];
    if (cw_rd(CW_REG_VCELL_H, b, 2) != 0) return -1;
    uint32_t raw = ((uint32_t)(b[0] & 0x3F) << 8) | b[1];
    return (int)(raw * 5 / 16);
}
// ID 引脚电压 mV(有符号 14bit)。仅用于开机诊断打印,不参与电量计算。
static int cw_read_id_mv(void) {
    uint8_t b[2];
    if (cw_rd(CW_REG_VOLT_ID_H, b, 2) != 0) return 0;
    int raw = (int)(((uint16_t)(b[0] & 0x3F) << 8) | b[1]);
    if (raw & 0x2000) raw -= 0x4000;           // bit13 符号位 → 补码还原
    return raw * 5 / 16;                        // *312.5uV → mV
}

#if CW2017_WRITE_PROFILE
static const cw2017_profile_t *active_profile(void) {
    int s = (CW2017_PROFILE_SEL >= 0 && CW2017_PROFILE_SEL < CW2017_PROFILE_COUNT)
            ? CW2017_PROFILE_SEL : 0;
    return &CW2017_PROFILES[s];
}

// 是否需要(重新)写 profile:UPDATE_FLAG 未置(说明芯片断过电/被复位),或芯片里的 profile
// 与选定的不一致,才需要写。否则跳过 —— 避免每次 MCU 重启都重写而清掉电量计已学习的状态。
static bool cw_need_update(const cw2017_profile_t *prof) {
    uint8_t alert = 0;
    if (cw_rd(CW_REG_SOC_ALERT, &alert, 1) != 0) return true;
    if (!(alert & 0x80)) return true;
    for (int i = 0; i < CW2017_PROFILE_LEN; i++) {
        uint8_t cur;
        if (cw_rd((uint8_t)(CW_REG_BATINFO + i), &cur, 1) != 0) return true;
        if (cur != prof->data[i]) return true;
    }
    return false;
}

// 写 profile:进复位态 → 写 80 字节到 0x10 → 触发重启重载 → 末步置 UPDATE_FLAG(重启会清默认,
// 故 flag 必须最后置,断电后自动清零,下次开机据此判定要重写)。
static void cw_write_profile(const cw2017_profile_t *prof) {
    ESP_LOGI(TAG, "写入电池 profile '%s'(%d 字节 → 0x10)", prof->name, CW2017_PROFILE_LEN);
    cw_wr(CW_REG_CONFIG, CW_CONFIG_RESTART);
    vTaskDelay(pdMS_TO_TICKS(30));
    for (int i = 0; i < CW2017_PROFILE_LEN; i++)
        cw_wr((uint8_t)(CW_REG_BATINFO + i), prof->data[i]);
    cw_wr(CW_REG_CONFIG, CW_CONFIG_NORMAL);       // 重启,重载 profile
    vTaskDelay(pdMS_TO_TICKS(30));
    uint8_t alert = 0;
    cw_rd(CW_REG_SOC_ALERT, &alert, 1);
    cw_wr(CW_REG_SOC_ALERT, alert | 0x80);        // 置 UPDATE_FLAG(标记已写)
}
#endif // CW2017_WRITE_PROFILE

// 上电:探测版本 → 配置 profile(或唤醒)→ 轮询 SOC 至有效。探测失败返回 -1。
static int cw_power_on(void) {
    uint8_t ver = 0;
    if (cw_rd(CW_REG_VERSION, &ver, 1) != 0) { ESP_LOGW(TAG, "未应答(总线上无 CW2017?)"); return -1; }
    ESP_LOGI(TAG, "检测到 CW2017 VERSION=0x%02X", ver);
    ESP_LOGI(TAG, "ID 引脚电压 = %d mV(诊断用)", cw_read_id_mv());
#if CW2017_WRITE_PROFILE
    const cw2017_profile_t *prof = active_profile();
    if (cw_need_update(prof)) cw_write_profile(prof);
    else ESP_LOGI(TAG, "profile '%s' 已在芯片中(UPDATE_FLAG 置位),保留已学习状态", prof->name);
#else
    cw_wr(CW_REG_CONFIG, CW_CONFIG_RESTART);      // 用芯片自带 profile:唤醒/重启
    vTaskDelay(pdMS_TO_TICKS(20));
    cw_wr(CW_REG_CONFIG, CW_CONFIG_NORMAL);
#endif
    for (int i = 0; i < 40; i++) {                // 首次 SOC 估算可能要几百 ms~2s
        uint8_t soc;
        if (cw_rd(CW_REG_SOC_H, &soc, 1) == 0 && soc <= 100) {
            ESP_LOGI(TAG, "SOC 就绪: %d%%", soc);
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(TAG, "SOC 首次估算超时(将随后台采样重试)");
    return 0;   // 仍返回成功:read_soc 会持续尝试,期间上层维持虚拟/旧值
}

static const hal_battery_api_t API = { .read_soc = cw_read_soc, .read_mv = cw_read_mv };
static hal_battery_t s_handle = { .api = &API, .impl = NULL };

hal_battery_t *platform_create_battery(const board_config_t *cfg) {
    // 复用与 ES8311 同一条 I2C 总线(I2C_NUM_0);若音频未启用/总线未建,则自建一条。
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK || !bus) {
        i2c_master_bus_config_t bc = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = cfg->i2c_sda,
            .scl_io_num = cfg->i2c_scl,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&bc, &bus) != ESP_OK) {
            ESP_LOGW(TAG, "I2C 总线不可用,电量禁用");
            return NULL;
        }
    }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CW_ADDR,
        .scl_speed_hz    = 100000,
    };
    if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "挂载 CW2017 设备失败,电量禁用");
        return NULL;
    }
    if (cw_power_on() != 0) {
        i2c_master_bus_rm_device(s_dev); s_dev = NULL;
        return NULL;
    }
    return &s_handle;
}
#else  // PERIPH_BATTERY==0
#include "platform/platform_factory.h"
hal_battery_t *platform_create_battery(const board_config_t *cfg) { (void)cfg; return 0; }
#endif
