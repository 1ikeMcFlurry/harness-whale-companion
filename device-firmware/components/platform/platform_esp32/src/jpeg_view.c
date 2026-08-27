// components/platform/platform_esp32/src/jpeg_view.c —— JPG 全屏看图
// 块解码写 imgframe(flash),再用 lv_image 从 flash 显示 —— 交给 LVGL 渲染。
// 不再直刷屏:直刷(hal_display_flush/esp_lcd)会与 esp_lvgl_port 争同一 panel
// (s_block 单缓冲异步 DMA 竞争 → 水平撕裂;draw 完成触发 LVGL flush-ready 回调 →
//  打乱其双缓冲状态机 → 覆盖/崩溃)。改用 lv_image 后 LVGL 统一管理,稳定无撕裂。
// 无 PSRAM 装不下整帧 RAM,故整帧存 flash(imgframe),lv_image 从 flash mmap 渲染。
#include "platform/board_config.h"
#if PERIPH_DISPLAY
#include "platform/platform_factory.h"
#include "hal/hal_display.h"
#include "jpeg_store.h"
#include "avatar_store.h"
#include "png_decode.h"
#include "services/jpeg_probe.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_common.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static const char *TAG = "jpeg_view";
#define SCR_W 240
#define SCR_H 320
#define TICK_MS 100

static lv_timer_t    *s_timer;
static volatile int   s_pending;
static volatile int   s_exit_req;
// 退出全屏后内部触发的头像重解码:不是用户下发的图片操作,**不能回报给小程序**,
// 否则小程序会收到一条凭空的"图片显示成功",可能误判成某次上传完成了。
static volatile int   s_restore_only;
static bool           s_active;
static lv_obj_t      *s_scr;         // 看图 screen
static lv_obj_t      *s_prev;        // 进看图前的 screen(主界面)
static lv_image_dsc_t s_dsc;         // 指向 flash 的 image 描述(active 期间保持)
static jpeg_view_result_cb_t s_result_cb;   // 解码显示结果回调(供上层回报小程序)
static void         *s_result_user;
static jpeg_view_avatar_cb_t s_avatar_cb;   // 头像位模式:解码后把 dsc 交给上层
static void         *s_avatar_user;
static int           s_req_mode;            // 本次请求模式(0=全屏 1=头像位)
static char          s_avatar_name[16] = "default";
static char          s_avatar_pending[16] = "default";
static portMUX_TYPE  s_avatar_mux = portMUX_INITIALIZER_UNLOCKED;
// 全屏图上的文字提示(toast):进入全屏短暂显示后自动消失
static const char   *s_hint_text;
static const void   *s_hint_font;
static lv_obj_t     *s_hint;                // 当前提示对象(s_scr 的子对象)
static int           s_hint_ticks;          // 剩余显示 tick 数
#define HINT_MS      3500                   // 提示停留时长
static lv_color_format_t s_frame_cf = LV_COLOR_FORMAT_RGB565;  // 本次解码帧格式(PNG头像=ARGB8888)
// 块解码输出条带缓冲(一次一个 MCU 条带 240×16×2)。静态 16 对齐,
// 避开 esp_new_jpeg 的 jpeg_calloc_align 在无 PSRAM 的 C3 上分配失败。
static uint8_t s_block[SCR_W * 16 * 2] __attribute__((aligned(16)));
static jpeg_probe_t s_last_probe = JPEG_PROBE_OK;   // 最近一次解码尝试的预检结果

// 解码期间置位:此时 imgstore 分区正被 mmap 读取,**绝不能让新的图片传输擦它**。
// 解码跑在 LVGL 任务,而 BLE 收图跑在 NimBLE host 任务 —— 两者无锁,曾出现过
// 新的 BEGIN 帧擦掉分区、把解码器脚下的数据抽走的情况(现象是 JPEG SOS 段解析
// 失败,看起来像"图片损坏",实际数据在解码开始时是好的,极难定位)。
static volatile bool s_decoding = false;

// 头像白底抑制:把极接近纯白的像素换成深色底,避免 JPEG 自带白背景在暗色主题上像张白卡片。
// 只对头像生效(全屏图不动)。深色底须与 ui_profile 的头像框底 COL_SLOT(#0C1712)一致,
// 换算 RGB565 = 0x08A2。阈值取极严(R≥247&&G≥247&&B≥247)——只吃背景纯白,尽量不动人物高光。
#define AVATAR_BG_565 0x08A2u
static void suppress_white(uint8_t *buf, int nbytes) {
    for (int i = 0; i + 1 < nbytes; i += 2) {
        uint16_t px = (uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8);   // RGB565 小端
        uint8_t r = (px >> 11) & 0x1F, g = (px >> 5) & 0x3F, b = px & 0x1F;
        if (r >= 30 && g >= 61 && b >= 30) {                            // 近纯白
            buf[i]     = (uint8_t)(AVATAR_BG_565 & 0xFF);
            buf[i + 1] = (uint8_t)(AVATAR_BG_565 >> 8);
        }
    }
}

// 解码 flash 中 JPG → RGB565_LE 整帧写入 imgframe。成功填 *ow/*oh 返回 0。
static int decode_to_flash(int *ow, int *oh) {
    s_last_probe = JPEG_PROBE_OK;
    s_decoding = true;                 // 从这里到函数返回,imgstore 不可被擦写
    const uint8_t *jpg; int jlen;
    bool source_mapped = false;
    if (s_req_mode == JPEG_VIEW_AVATAR) {
        if (avatar_store_get(s_avatar_name, &jpg, &jlen) != 0) {
            ESP_LOGW(TAG, "avatar '%s' not found", s_avatar_name);
            s_decoding = false;
            return -1;
        }
        if (jlen < 8 || memcmp(jpg, "\x89PNG\r\n\x1a\n", 8) != 0) {
            ESP_LOGW(TAG, "avatar '%s' is not PNG", s_avatar_name);
            s_decoding = false;
            return -1;
        }
    } else if (jpeg_store_mmap(JPEG_SLOT_FULL, &jpg, &jlen) == 0) {
        source_mapped = true;
    } else {
        ESP_LOGW(TAG, "jpg mmap fail");
        s_decoding = false;
        return -1;
    }

    // 内置头像必须是 PNG(透明背景);全屏仍是 BLE 写入的 JPEG。
    if (s_req_mode == JPEG_VIEW_AVATAR && jlen >= 8 && memcmp(jpg, "\x89PNG\r\n\x1a\n", 8) == 0) {
        int r = png_decode_to_frame(jpg, jlen, ow, oh);
        s_decoding = false;
        if (r == 0) { s_frame_cf = LV_COLOR_FORMAT_ARGB8888; return 0; }
        ESP_LOGW(TAG, "PNG 头像解码失败 rc=%d", r);
        return -1;
    }
    s_frame_cf = LV_COLOR_FORMAT_RGB565;   // JPEG 路径(全屏图 + JPEG 头像)

    // 提前拦下解码器啃不动的格式,给出确切原因 —— 否则只会得到一个
    // 笼统的 "process[0]=-5",没人猜得到是渐进式
    jpeg_probe_t p = jpeg_probe(jpg, jlen);
    s_last_probe = p;
    if (p != JPEG_PROBE_OK) {
        ESP_LOGW(TAG, "JPG 预检不通过(%d 字节): %s", jlen, jpeg_probe_str(p));
        if (source_mapped) jpeg_store_unmap();
        s_decoding = false;
        return -1;
    }

    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type  = JPEG_PIXEL_FORMAT_RGB565_LE;   // LVGL 内部小端;port swap_bytes 输出到 panel
    cfg.block_enable = true;
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "open fail"); if (source_mapped) jpeg_store_unmap(); s_decoding = false; return -1;
    }

    jpeg_dec_io_t io; memset(&io, 0, sizeof io);
    io.inbuf = (uint8_t *)jpg; io.inbuf_len = jlen;
    jpeg_dec_header_info_t info;
    int rc = -1;
    jpeg_error_t pe = jpeg_dec_parse_header(dec, &io, &info);
    if (pe == JPEG_ERR_OK && info.width > 0 && info.height > 0
        && info.width <= SCR_W && info.height <= SCR_H) {
        int outlen = 0, count = 0;
        jpeg_dec_get_outbuf_len(dec, &outlen);
        jpeg_dec_get_process_count(dec, &count);
        uint32_t frame_bytes = (uint32_t)info.width * info.height * 2;
        ESP_LOGI(TAG, "decode->flash %dx%d frame=%uB count=%d", info.width, info.height,
                 (unsigned)frame_bytes, count);
        if (outlen > 0 && outlen <= (int)sizeof s_block && jpeg_frame_begin(frame_bytes) == 0) {
            io.outbuf = s_block;
            uint32_t off = 0; int ok = 1;
            for (int i = 0; i < count; i++) {
                jpeg_error_t r = jpeg_dec_process(dec, &io);
                if (io.out_size > 0) {
                    if (s_req_mode == JPEG_VIEW_AVATAR)   // 仅头像:抑掉 JPEG 白底,融入暗卡
                        suppress_white(s_block, io.out_size);
                    if (jpeg_frame_write(off, s_block, io.out_size) != 0) { ok = 0; break; }
                    off += io.out_size;
                }
                if (r != JPEG_ERR_OK) { ESP_LOGW(TAG, "process[%d]=%d out=%d", i, r, io.out_size); break; }
            }
            if (ok && off >= frame_bytes) { rc = 0; *ow = info.width; *oh = info.height; }
            else ESP_LOGW(TAG, "write incomplete off=%u/%u ok=%d", (unsigned)off, (unsigned)frame_bytes, ok);
        } else {
            ESP_LOGW(TAG, "frame_begin fail or block too big (outlen=%d)", outlen);
        }
    } else {
        ESP_LOGW(TAG, "parse ret=%d size %dx%d", pe, info.width, info.height);
    }
    jpeg_dec_close(dec);
    if (source_mapped) jpeg_store_unmap();
    s_decoding = false;
    return rc;
}

// 最近一次解码尝试的预检结果,供上层把失败原因回报给小程序。
jpeg_probe_t jpeg_view_last_probe(void) { return s_last_probe; }

// 解码 flash 中 JPG/PNG → mmap → 填充 s_dsc(data 指向 flash)。成功返回 0。
// 格式由 decode_to_flash 设的 s_frame_cf 决定:JPEG=RGB565(2B),PNG头像=ARGB8888(4B,带 alpha)。
static int build_dsc(void) {
    int w = 0, h = 0;
    if (decode_to_flash(&w, &h) != 0) return -1;
    int bpp = (s_frame_cf == LV_COLOR_FORMAT_ARGB8888) ? 4 : 2;
    const uint8_t *fp;
    if (jpeg_frame_mmap(&fp, (uint32_t)w * h * bpp) != 0) { ESP_LOGW(TAG, "frame mmap fail"); return -1; }
    memset(&s_dsc, 0, sizeof s_dsc);
    s_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf     = s_frame_cf;
    s_dsc.header.w      = w;
    s_dsc.header.h      = h;
    s_dsc.header.stride = w * bpp;
    s_dsc.data          = fp;
    s_dsc.data_size     = (uint32_t)w * h * bpp;
    return 0;
}

// 全屏:解码后作为 lv_image 显示到新 screen。成功返回 0。
static int show_image(void) {
    // 连续收到多张全屏图(未退出)时的复用/替换:记住旧全屏屏,稍后删除,且**不**把它当返回目标。
    // 否则 s_prev 会指向一张仍 lv_image_set_src(&s_dsc) 的旧全屏屏 —— 退出时加载它 + 解除 mmap
    // → 重绘读悬空 flash → Cache error/MMU fault(实测崩溃点)。
    lv_obj_t *old_scr = s_scr;
    if (!old_scr) s_prev = lv_screen_active();   // 仅首次进全屏记录返回目标(通常是主界面)
    if (build_dsc() != 0) return -1;
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *img = lv_image_create(s_scr);
    lv_image_set_src(img, &s_dsc);
    lv_obj_center(img);

    // 底部叠加文字提示(如"可以扫码了解更多哦"),半透明深色条,HINT_MS 后自动消失。
    s_hint = NULL;
    if (s_hint_text && s_hint_font) {
        s_hint = lv_label_create(s_scr);
        lv_label_set_text(s_hint, s_hint_text);
        lv_obj_set_style_text_font(s_hint, (const lv_font_t *)s_hint_font, 0);
        lv_obj_set_style_text_color(s_hint, lv_color_white(), 0);
        lv_obj_set_style_bg_color(s_hint, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
        lv_obj_set_style_radius(s_hint, 10, 0);
        lv_obj_set_style_pad_hor(s_hint, 18, 0);           // 更大内边距 → 更醒目
        lv_obj_set_style_pad_ver(s_hint, 12, 0);
        lv_obj_set_style_border_color(s_hint, lv_color_hex(0x35E07E), 0);  // 霓虹绿边框
        lv_obj_set_style_border_width(s_hint, 2, 0);
        lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -22);
        s_hint_ticks = HINT_MS / TICK_MS;
    }

    lv_screen_load(s_scr);
    if (old_scr && old_scr != s_scr) lv_obj_delete(old_scr);   // 删上一张全屏(引用 &s_dsc,现已不显示)
    return 0;
}

void jpeg_view_set_hint(const char *text, const void *font) {
    s_hint_text = text; s_hint_font = font;
}

static void teardown(void) {
    if (s_prev) lv_screen_load(s_prev);
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_hint = NULL; }  // s_hint 是 s_scr 子对象,随之删除

    // ⚠ 关键顺序:先让头像位放下对 imgframe 的引用,再解除映射。
    // 头像位的 lv_image(s_dsc)指向 imgframe 的 mmap;若先 munmap 再返回,主界面这次
    // 重绘会去读已解除映射的 flash 地址(头像走缩放 transform_rgb565a8)→ Cache error/
    // MMU fault(实测崩溃点)。s_avatar_cb(NULL) 会删掉头像 lv_image,这帧先画成空框,
    // 下一 tick 重解码头像再显示(~100ms 的占位闪烁,可接受)。
    if (s_avatar_cb) s_avatar_cb(NULL, s_avatar_user);
    jpeg_frame_unmap();
    s_active = false;

    // 全屏解码时把 imgframe 覆盖了(与头像共用同一块解码缓冲),退出后必须重新解码头像图,
    // 否则头像位显示的是全屏图的一角。
    if (avatar_store_has(s_avatar_name)) {
        s_req_mode    = JPEG_VIEW_AVATAR;
        s_restore_only = 1;           // 内部恢复,不回报小程序
        s_pending      = 1;           // 下一个 tick 会重新解码并经 s_avatar_cb 刷新
    }
}

static void tick(lv_timer_t *t) {
    (void)t;
    if (s_pending) {
        s_pending = 0;
        if (s_req_mode == JPEG_VIEW_AVATAR) {   // 头像位:解码后交回调显示,常驻不计时
            portENTER_CRITICAL(&s_avatar_mux);
            memcpy(s_avatar_name, s_avatar_pending, sizeof s_avatar_name);
            portEXIT_CRITICAL(&s_avatar_mux);
            int restore = s_restore_only; s_restore_only = 0;
            int ok = (build_dsc() == 0);
            ESP_LOGI(TAG, "avatar image %s%s", ok ? "OK" : "decode fail",
                     restore ? " (退出全屏后的内部恢复)" : "");
            if (s_avatar_cb) s_avatar_cb(ok ? (const void *)&s_dsc : NULL, s_avatar_user);
            // 内部恢复不回报:小程序没下发任何东西,凭空收到"显示成功"会误判
            (void)restore; // 内置头像从不属于 BLE 图片操作,不发送图片状态通知。
        } else {                                 // 全屏:显示到新 screen,常驻直到用户退出
            int ok = (show_image() == 0);
            if (ok) {
                s_active = true; s_exit_req = 0;
                ESP_LOGI(TAG, "view ACTIVE (常驻,等待用户退出)");
            } else {
                ESP_LOGW(TAG, "show failed -> stay on main");
            }
            if (s_result_cb) s_result_cb(ok, s_result_user);
        }
    } else if (s_active) {
        // 文字提示 toast:显示 HINT_MS 后自动消失(全屏图本身常驻)
        if (s_hint && --s_hint_ticks <= 0) { lv_obj_delete(s_hint); s_hint = NULL; }
        // 全屏图常驻显示,不再自动超时退出 —— 只有用户长按确定键才退
        if (s_exit_req) {
            ESP_LOGI(TAG, "view EXIT (用户主动退出)");
            teardown();
        }
    }
}

void jpeg_view_set_result_cb(jpeg_view_result_cb_t cb, void *user) {
    s_result_cb = cb; s_result_user = user;
}
void jpeg_view_init(hal_display_t *disp) {
    (void)disp;   // 不再直刷,改用 lv_image;disp 保留仅为接口兼容
    s_timer = lv_timer_create(tick, TICK_MS, NULL);
}
void jpeg_view_set_avatar_cb(jpeg_view_avatar_cb_t cb, void *user) {
    s_avatar_cb = cb; s_avatar_user = user;
}
void jpeg_view_request_mode(jpeg_view_mode_t mode) {
    s_req_mode = (int)mode; s_restore_only = 0; s_pending = 1;
}
void jpeg_view_request_avatar(const char *name) {
    if (!name) return;
    size_t n = strnlen(name, sizeof s_avatar_name);
    if (n == 0 || n >= sizeof s_avatar_name) return;
    portENTER_CRITICAL(&s_avatar_mux);
    memcpy(s_avatar_pending, name, n);
    s_avatar_pending[n] = '\0';
    portEXIT_CRITICAL(&s_avatar_mux);
    s_req_mode = JPEG_VIEW_AVATAR;
    s_restore_only = 1;
    s_pending = 1;
}
void jpeg_view_request(void) { jpeg_view_request_mode(JPEG_VIEW_FULLSCREEN); }
void jpeg_view_exit(void)    { s_exit_req = 1; }
bool jpeg_view_is_active(void) { return s_active || s_pending; }

// 是否正在(或即将)读取 imgstore。s_pending 也要算进来:请求已排队但 tick 尚未执行时
// 擦掉分区,后果与解码中被擦完全一样。
bool jpeg_view_decode_busy(void) { return s_pending || s_decoding; }
#endif // PERIPH_DISPLAY
