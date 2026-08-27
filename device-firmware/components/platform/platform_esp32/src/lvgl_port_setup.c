// components/platform_esp32/src/lvgl_port_setup.c —— 仅此处含 lvgl/esp_lvgl_port
#include "platform/board_config.h"
#if PERIPH_DISPLAY
#include "esp_lvgl_port.h"
#include "platform/platform_factory.h"
#include "platform/platform_screen_capture.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include <stdbool.h>

static bool s_nav_enabled = true;

// 由 disp_st7789.c 提供:取出底层 panel/io(平台层内部可见)
esp_lcd_panel_handle_t    platform_display_panel(hal_display_t *d);
esp_lcd_panel_io_handle_t platform_display_io(hal_display_t *d);

// LVGL 内存池用量。builtin malloc = 独立静态池(不与系统堆共享,用不完的部分被"搁浅");
// 切到 CLIB malloc 后 LVGL 走系统堆,此处返回 0(直接看系统 free 即可)。
void platform_lvgl_mem_stats(uint32_t *used, uint32_t *free_sz, uint32_t *max_blk) {
// 注意宏名:LVGL 9 用 LV_USE_STDLIB_MALLOC==LV_STDLIB_BUILTIN 选择分配器,
// 没有 LV_USE_BUILTIN_MALLOC 这个宏(写错会静默走 #else 恒返回 0)。
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    // 报 max_used(历史峰值)而非 total-free(采样瞬时值):LVGL 绘制期间会临时分配
    // layer/draw 缓冲,两次采样之间就释放了,瞬时值会严重低估真实需求 —— 据此调小池
    // 会导致 TLSF 找不到块、绘制卡死触发看门狗(已踩过)。定池大小只看 max_used。
    if (used)    *used    = (uint32_t)m.max_used;
    if (free_sz) *free_sz = (uint32_t)m.total_size;   // 池容量(便于直接看 峰值/容量)
    if (max_blk) *max_blk = (uint32_t)m.free_biggest_size;
#else
    if (used)    *used    = 0;
    if (free_sz) *free_sz = 0;
    if (max_blk) *max_blk = 0;
#endif
}

// 给当前激活屏统一加圆角 + 裁剪(圆角外露的 4 角由底层黑填充)。
// 圆角半径见 board_config.h 的 SCREEN_CORNER_RADIUS,改那一个值即可。
static void round_active_screen(lv_event_t *e) {
    (void)e;
#if SCREEN_CORNER_RADIUS > 0
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;
    lv_obj_set_style_radius(scr, SCREEN_CORNER_RADIUS, 0);
    lv_obj_set_style_clip_corner(scr, true, 0);
#endif
}

lv_display_t *platform_lvgl_init(hal_display_t *disp, const board_config_t *cfg) {
    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&pc);

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = platform_display_panel(disp),
        .io_handle    = platform_display_io(disp),
        // C3 无 PSRAM,全部 DMA 挤内部 ~150KB;屏幕+音频 I2S+BLE 一起时,
        // 40 行双缓冲(≈37.5KB DMA)会把 I2S DMA 描述符挤爆(NO_MEM)。
        // 缩到 20 行单缓冲(≈9.6KB),把连续 DMA 内存让给音频。刷新略慢但够用。
        .buffer_size  = (uint32_t)cfg->lcd_w * 20,
        .double_buffer = false,
        .hres = cfg->lcd_w, .vres = cfg->lcd_h,
        // 屏幕旋转必须在此处配置:esp_lvgl_port 注册显示时会用这里的值重新下发
        // swap_xy/mirror,覆盖 disp_st7789.c 里对 MADCTL(0x36 / esp_lcd_panel_mirror)
        // 的任何设置。竖屏旋转 180° = 同时镜像 X、Y,不交换 XY。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes: LVGL(小端 RGB565) → ST7789(SPI 大端)需交换高低字节。
        // LVGL 9 已无 CONFIG_LV_COLOR_16_SWAP,字节交换在此处控制。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    lv_display_t *d = lvgl_port_add_disp(&dc);
    if (d && lvgl_port_lock(0)) {
        // 截图 flush hook 与其他 LVGL 对象/事件设置一样在 LVGL 锁内注册。
        // 该调用不受圆角开关影响,SCREEN_CORNER_RADIUS=0 时仍会执行。
        platform_screen_capture_init(d, NULL, NULL, NULL);
#if SCREEN_CORNER_RADIUS > 0
        // 底层填黑:屏幕圆角外露的 4 角显示为黑边(适配外壳圆角开窗)
        lv_obj_t *bottom = lv_layer_bottom();
        lv_obj_set_style_bg_color(bottom, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, 0);
        // 每次任意屏幕加载 → 统一加圆角;当前已激活屏也补一次
        lv_display_add_event_cb(d, round_active_screen, LV_EVENT_SCREEN_LOADED, NULL);
        round_active_screen(NULL);
#endif
        lvgl_port_unlock();
    }
    return d;
}

// LVGL 锁封装:LVGL 非线程安全,组装层绘制 UI 前后用它加/解锁。
bool platform_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void platform_lvgl_unlock(void) { lvgl_port_unlock(); }

#if PERIPH_BUTTON
#include "esp_lvgl_port_button.h"
#include "iot_button.h"
#include "hal/hal_button.h"

// 由 btn_iot_button.c 提供:取底层 iot_button 句柄
button_handle_t platform_button_iot_handle(hal_button_t *b, int index);

// 保存导航 encoder,供游戏期间临时屏蔽 dock 导航(见 platform_lvgl_nav_enable)。
static lv_indev_t *s_nav_indev;

// 使能/屏蔽按键的 LVGL 导航。进入小游戏时置 false(改由 hal_button 直接读键),
// 退出时置 true 恢复 dock 导航。
void platform_lvgl_nav_enable(bool en) {
    s_nav_enabled = en;
    if (s_nav_indev) lv_indev_enable(s_nav_indev, en);
}

// 把 3 个按键接入 LVGL 编码器型输入设备:上=prev、下=next、确定=enter。
// 须在创建 demo/界面之前调用:这里把新建的 group 设为默认 group,
// 之后创建的可聚焦控件会自动加入,从而可用按键导航。
void platform_lvgl_attach_buttons(hal_button_t *btn, lv_display_t *disp) {
    if (!btn || !disp) return;

    const lvgl_port_nav_btns_cfg_t cfg = {
        .disp         = disp,
        .button_prev  = platform_button_iot_handle(btn, 0),   // 上
        .button_next  = platform_button_iot_handle(btn, 1),   // 下
        .button_enter = platform_button_iot_handle(btn, 2),   // 确定
    };
    // 三个句柄必须齐全:若按键创建失败(如 ADC 配置错),传 NULL 进去会让
    // lvgl_port 走清理路径 iot_button_delete(NULL) → CORRUPT HEAP 崩溃。
    if (!cfg.button_prev || !cfg.button_next || !cfg.button_enter) {
        ESP_LOGE("lvgl_btn", "按键句柄不全(prev=%p next=%p enter=%p),跳过 LVGL 导航接入",
                 cfg.button_prev, cfg.button_next, cfg.button_enter);
        return;
    }
    lv_indev_t *indev = lvgl_port_add_navigation_buttons(&cfg);
    if (!indev) return;
    s_nav_indev = indev;

    if (lvgl_port_lock(0)) {
        lv_indev_enable(indev, s_nav_enabled);
        lv_group_t *g = lv_group_create();
        lv_group_set_default(g);          // 之后建的控件自动入组
        lv_indev_set_group(indev, g);     // 编码器在该组内导航
        lvgl_port_unlock();
    }
}
#endif // PERIPH_BUTTON

#if !PERIPH_BUTTON
// 无按钮外设时的空实现:没有 LVGL 导航输入设备可切换。app.c 在 PERIPH_DISPLAY 下(不受
// PERIPH_BUTTON 守卫)会调用它,故此处必须提供定义,保证 DISPLAY=1/BUTTON=0 组合可链接。
void platform_lvgl_nav_enable(bool en) { s_nav_enabled = en; }
#endif

bool platform_lvgl_nav_enabled(void) { return s_nav_enabled; }
#endif // PERIPH_DISPLAY
