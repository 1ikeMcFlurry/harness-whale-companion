// components/platform_esp32/src/disp_st7789.c
//
// 屏幕:2.00" TFT T200H7-C14-05,驱动 IC ST7789P3,4 线 SPI,分辨率 240×320。
// 使用 ESP-IDF 内置 esp_lcd st7789 驱动(esp_lcd_new_panel_st7789),
// 面板专属的 porch/power/gamma 等寄存器按参考例程 User/main.c 的 TFT_init()
// 逐条转写,在内置基础初始化之后经 esp_lcd_panel_io_tx_param() 注入。
#include "platform/board_config.h"
#if PERIPH_DISPLAY
#include "hal/hal_display.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"     // esp_lcd_new_panel_st7789 / esp_lcd_panel_dev_config_t
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *BLTAG = "disp_bl";   // 背光诊断日志
static const char *TAG   = "disp_st7789";

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    uint16_t w, h;
    int bl_gpio;
    bool bl_ready;
} st7789_ctx_t;

// ============================================================================
// ST7789P3 面板专属调优序列(T200H7-C14-05)。
// 与参考例程 TFT_init() 逐条对应,但以下由内置驱动完成、此处不再重复:
//   0x11 SLPOUT / 0x3A COLMOD  → esp_lcd_panel_init()
//   0x36 MADCTL(0xC0)          → esp_lcd_panel_mirror(true,true)
//   0x21 INVON                 → esp_lcd_panel_invert_color()
//   0x29 DISPON                → esp_lcd_panel_disp_on_off()
// 仅保留 porch/gate/VCOM/power/gamma 等寄存器写入。
// ============================================================================
typedef struct {
    uint8_t  cmd;
    uint8_t  data[16];
    uint8_t  len;
    uint16_t delay_ms;
} st_init_cmd_t;

static const st_init_cmd_t st7789p3_tuning_cmds[] = {
    {0xB2, {0x05, 0x05, 0x00, 0x33, 0x33}, 5, 0},   // PORCTRL 帧率 porch
    {0xB7, {0x35}, 1, 0},                            // GCTRL 栅极
    {0xBB, {0x21}, 1, 0},                            // VCOMS
    {0xC0, {0x2C}, 1, 0},                            // LCMCTRL
    {0xC2, {0x01}, 1, 0},                            // VDVVRHEN
    {0xC3, {0x0B}, 1, 0},                            // VRHS
    {0xC4, {0x20}, 1, 0},                            // VDVSET
    {0xC6, {0x0F}, 1, 0},                            // FRCTRL2 60Hz 点反转
    {0xD0, {0xA7, 0xA1}, 2, 0},                      // PWCTRL1
    {0xD0, {0xA4, 0xA1}, 2, 0},                      // PWCTRL1(参考例程重发,覆盖上一条)
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43,   // PVGAMCTRL 正伽马
            0x49, 0x09, 0x16, 0x15, 0x26, 0x2B}, 14, 0},
    {0xE1, {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44,   // NVGAMCTRL 负伽马
            0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A}, 14, 10},
};

#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_RES     LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ 5000

static void bl_init(st7789_ctx_t *c) {
    if (c->bl_gpio < 0) { ESP_LOGW(BLTAG, "bl_gpio<0, 背光不受控"); return; }
    ledc_timer_config_t t = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RES,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) { ESP_LOGE(BLTAG, "ledc_timer_config 失败: %d", e); return; }
    ledc_channel_config_t ch = {
        .gpio_num   = c->bl_gpio,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    e = ledc_channel_config(&ch);
    if (e != ESP_OK) { ESP_LOGE(BLTAG, "ledc_channel_config 失败: %d", e); return; }
    c->bl_ready = true;
    ESP_LOGI(BLTAG, "背光 LEDC 就绪 gpio=%d", c->bl_gpio);
}

static void st_flush(hal_display_t *self, int x1, int y1, int x2, int y2, const void *px) {
    st7789_ctx_t *c = self->impl;
    // 注意 esp_lcd 的 end 坐标是开区间,故 +1
    esp_lcd_panel_draw_bitmap(c->panel, x1, y1, x2 + 1, y2 + 1, px);
}
static void st_get_size(hal_display_t *self, uint16_t *w, uint16_t *h) {
    st7789_ctx_t *c = self->impl; *w = c->w; *h = c->h;
}
static void st_backlight(hal_display_t *self, uint8_t p) {
    st7789_ctx_t *c = self->impl;
    if (p > 100) p = 100;
    uint32_t max_duty = (1u << BL_LEDC_RES) - 1u;
    uint32_t duty = (max_duty * p) / 100u;
    ESP_LOGI(BLTAG, "set_backlight %u%% (ready=%d duty=%u/%u)", p, c->bl_ready, duty, max_duty);
    if (!c->bl_ready) return;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

static const hal_display_api_t API = {
    .flush = st_flush, .get_size = st_get_size, .set_backlight = st_backlight,
};

// 工厂:对外只暴露 hal_display_t*,内部细节完全隐藏
hal_display_t *platform_create_display(const board_config_t *cfg) {
    st7789_ctx_t *c = calloc(1, sizeof(*c));
    c->w = cfg->lcd_w; c->h = cfg->lcd_h;
    c->bl_gpio = cfg->lcd_bl;

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->lcd_mosi, .sclk_io_num = cfg->lcd_sclk,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = cfg->lcd_w * 80 * 2,
    };
    spi_bus_initialize(cfg->lcd_spi_host, &bus, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = cfg->lcd_cs, .dc_gpio_num = cfg->lcd_dc,
        .pclk_hz = cfg->lcd_pclk_hz, .spi_mode = 0,     // ST7789 SCK 空闲低、上升沿采样 → mode0
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .trans_queue_depth = 10,
    };
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->lcd_spi_host, &io_cfg, &c->io);

    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = cfg->lcd_rst,               // -1 → 软复位(SWRESET)
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_st7789(c->io, &dev, &c->panel);

    esp_lcd_panel_reset(c->panel);                    // 硬复位或 SWRESET(rst=-1)
    esp_lcd_panel_init(c->panel);                     // SLPOUT / MADCTL / COLMOD / RAMCTRL

    // 注入 ST7789P3 面板专属 porch/power/gamma(参考例程 TFT_init())
    for (size_t i = 0; i < sizeof(st7789p3_tuning_cmds) / sizeof(st7789p3_tuning_cmds[0]); i++) {
        const st_init_cmd_t *cmd = &st7789p3_tuning_cmds[i];
        esp_err_t e = esp_lcd_panel_io_tx_param(c->io, cmd->cmd, cmd->data, cmd->len);
        if (e != ESP_OK) ESP_LOGE(TAG, "init cmd 0x%02X 失败: %d", cmd->cmd, e);
        if (cmd->delay_ms) vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
    }

    esp_lcd_panel_invert_color(c->panel, LCD_INVERT_COLOR);   // 0x21/0x20
    // 屏幕旋转/镜像统一在 lvgl_port_setup.c 的 dc.rotation 配置(esp_lvgl_port
    // 注册显示时会覆盖此处的 MADCTL),故此处不再调用 esp_lcd_panel_mirror。
    esp_lcd_panel_set_gap(c->panel, 0, 0);                    // 无行列偏移
    esp_lcd_panel_disp_on_off(c->panel, true);                // 0x29 DISPON

    bl_init(c);

    hal_display_t *d = calloc(1, sizeof(*d));
    d->api = &API; d->impl = c;
    return d;
}

// 给 lvgl_port 用:暴露底层 panel/io(只在平台层内部互相可见)
esp_lcd_panel_handle_t    platform_display_panel(hal_display_t *d) { return ((st7789_ctx_t*)d->impl)->panel; }
esp_lcd_panel_io_handle_t platform_display_io(hal_display_t *d)    { return ((st7789_ctx_t*)d->impl)->io; }
#endif // PERIPH_DISPLAY
