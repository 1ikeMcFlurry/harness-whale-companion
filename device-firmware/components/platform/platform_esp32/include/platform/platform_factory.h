// components/platform_esp32/include/platform/platform_factory.h
#pragma once
#include "hal/hal_display.h"
#include "hal/hal_button.h"
#include "hal/hal_audio.h"
#include "hal/hal_config.h"
#include "hal/hal_ble_scan.h"
#include "hal/hal_kv.h"
#include "hal/hal_identity.h"
#include "hal/hal_battery.h"
#include "services/jpeg_probe.h"
#include "platform/board_config.h"

hal_display_t   *platform_create_display(const board_config_t *);
// 电池电量读取(PERIPH_BATTERY=0 或创建失败时返回 NULL,上层据此跳过电量刷新)。
hal_battery_t   *platform_create_battery(const board_config_t *);
hal_button_t    *platform_create_button(const board_config_t *);

// 开机读一次按键 ADC,返回被按住的按键索引(0上/1下/2确定),无按下=-1。
// 须在 platform_create_button 之前调用(用完即释放 ADC unit)。产线自检触发用。
int              platform_button_boot_index(const board_config_t *);
bool             platform_button_any_pressed(hal_button_t *);
bool             platform_button_prepare_deep_sleep(hal_button_t *);
hal_audio_t     *platform_create_audio(const board_config_t *);

// If boot_button_index is Up (0) and a valid supported iNES image is present
// in the "nesrom" partition, start the low-memory NES runtime. The runtime
// saves battery RAM and restarts on exit, so a successful call never returns.
bool             platform_nes_try_boot(const board_config_t *, int boot_button_index);

// Start the dedicated Tamagotchi P1 runtime when a valid 12 KiB ROM is in the
// "tamarom" partition. Three physical buttons map directly to Tama A/B/C.
// A successful call owns the device and never returns.
bool             platform_tamagotchi_try_boot(const board_config_t *);

// 音频直通:麦克风采集实时输出到扬声器(对着麦说话,扬声器同步放出)。进入后不返回。
int              platform_audio_passthrough(hal_audio_t *a);

// LVGL 接入(返回类型来自 lvgl,故仅平台层包含)
struct _lv_display_t;
struct _lv_display_t *platform_lvgl_init(hal_display_t *, const board_config_t *);

// LVGL 锁:绘制/操作 LVGL 对象前后调用(LVGL 非线程安全)。
#include <stdbool.h>
bool platform_lvgl_lock(int timeout_ms);
void platform_lvgl_unlock(void);

// LVGL 内存池用量(供内存诊断)。builtin malloc 模式下报的是那块静态池的占用;
// 切到 CLIB malloc(走系统堆)后三项都返回 0 —— 因为已并入系统堆,看 free 即可。
// used=已用字节 / free_sz=池内剩余 / max_blk=池内最大连续块。app 层不含 lvgl.h,故封装在此。
void platform_lvgl_mem_stats(uint32_t *used, uint32_t *free_sz, uint32_t *max_blk);

// 把按键接入 LVGL 导航(上/下/确定)。须在 platform_lvgl_init 之后、显示界面之前调用。
void platform_lvgl_attach_buttons(hal_button_t *btn, struct _lv_display_t *disp);

// 使能/屏蔽按键的 LVGL 导航。进入小游戏时置 false(改由 hal_button 直接读键避免 dock
// 误触发),退出时置 true 恢复。
void platform_lvgl_nav_enable(bool en);
bool platform_lvgl_nav_enabled(void);

// 硬件随机数种子:每次开机不同。与外设开关无关,始终可用(通用工具,当前无调用方)。
#include <stdint.h>
uint32_t platform_random_seed(void);

// BLE 档案卡控制:启动 NimBLE + GATT 服务,返回 hal_config 端口(与外设开关 PERIPH_BLE 相关)。
// dev_name = 广播里的设备名,传本机 SN(网关扫一下即可识别是哪张卡)。
// 传 NULL 或空串则回落到 BLE_DEVICE_NAME。
hal_config_t *platform_create_ble_config(const board_config_t *cfg, const char *dev_name);

// BLE 扫描(observer):监听外部厂商广播,匹配到 → 回调。与 PERIPH_BLE 相关。
hal_ble_scan_t *platform_create_ble_scan(const board_config_t *cfg);

// 键值持久化(NVS 实现)。始终可用(不受 PERIPH_* 影响)。
hal_kv_t *platform_create_kv(const board_config_t *cfg);

// BLE 图片接收只保留全屏图。头像来自随固件烧录的 imgava 资源包。
typedef enum {
    JPEG_SLOT_FULL   = 0,   // 全屏图 → imgstore 分区
} jpeg_slot_t;

// 内置头像资源包。名称使用 [a-z0-9_-]{1,15},PNG 数据常驻映射于 imgava 分区。
int  avatar_store_init(void);
bool avatar_store_has(const char *name);
int  avatar_store_first_name(char name[16]);
int  avatar_store_name_at(uint8_t index, char name[16]);

// JPG 接收存储。begin 记住槽,后续 write/end 沿用(BLE 收图严格串行)。
// 供组合根把 BLE 分片写入 flash(仅 PERIPH_DISPLAY 下有定义)。
int  jpeg_store_begin(jpeg_slot_t slot, uint32_t total_len);
int  jpeg_store_write(const uint8_t *d, int n);
int  jpeg_store_end(void);
bool jpeg_store_has_valid(jpeg_slot_t slot);

// JPG 全屏看图(非阻塞,tick 驱动;仅 PERIPH_DISPLAY 下有定义)。
// 解码显示结果回调(ok=1 已显示 / ok=0 解码失败),供上层回报小程序。运行于 LVGL 任务。
typedef void (*jpeg_view_result_cb_t)(int ok, void *user);
void jpeg_view_set_result_cb(jpeg_view_result_cb_t cb, void *user);

// 图片显示模式
typedef enum { JPEG_VIEW_FULLSCREEN = 0, JPEG_VIEW_AVATAR = 1 } jpeg_view_mode_t;
void jpeg_view_request_mode(jpeg_view_mode_t mode);   // 按模式请求显示(异步,tick 里解码)
void jpeg_view_request_avatar(const char *name);      // 按内置资源名异步显示头像
// 头像位模式:解码后通过此回调把 dsc(const lv_image_dsc_t*)交给上层显示;dsc=NULL 表示解码失败。
typedef void (*jpeg_view_avatar_cb_t)(const void *dsc, void *user);
void jpeg_view_set_avatar_cb(jpeg_view_avatar_cb_t cb, void *user);

void jpeg_view_init(hal_display_t *disp);
void jpeg_view_request(void);
void jpeg_view_exit(void);
bool jpeg_view_is_active(void);
// 全屏图上叠加的文字提示(如"可以扫码了解更多哦"),进入全屏时短暂显示后自动消失。
// font 实为 const lv_font_t*(用 void* 免去本头依赖 lvgl);text/font 传 NULL 则不显示。
void jpeg_view_set_hint(const char *text, const void *font);

// 最近一次 JPG 解码尝试的预检结果(见 services/jpeg_probe.h)。
// 解码失败时用它区分"渐进式/算术编码"与"数据截断",回报给小程序。
jpeg_probe_t jpeg_view_last_probe(void);

// 是否正在(或即将)读取 imgstore 分区。解码跑在 LVGL 任务、收图跑在 NimBLE host 任务,
// 两者无锁 —— 收图时若擦掉分区,会把解码器脚下的数据抽走。组装根必须在放行
// 新的图片传输 BEGIN 之前查这个。
bool jpeg_view_decode_busy(void);

// 设备身份(cardid 分区)。函数本身不依赖任何外设,但目前 app.c 只在 PERIPH_BLE
// 打开时才创建它 —— 关掉 BLE 就没有身份、没有串口指令、也没有 SN 自补。
// (身份的两个消费方都在 BLE 侧:只读特征 …0012 与 token 广播验签,故如此接线。)
// 内部会在 SN 缺失时按 esp_read_mac(ESP_MAC_WIFI_STA) 自补。
hal_identity_t *platform_create_identity(const board_config_t *cfg);

// ProductKey 的 SHA256 前 4 字节十六进制(8 字符 + '\0'),供串口回读校验。
// 刻意只给指纹不给明文,见 identity_nvs.c 注释。
void identity_pk_fingerprint(char out[9]);

// cardid 四字段各自是否已写入(非空):DeviceKey(sn)/DeviceSecret(key)/
// ProductKey(pk)/HardwareVersion(hw)。供产测逐字段检查存在性,缺字段判 FAIL。
// 不暴露 ProductKey 明文,只报在/不在。传 NULL 的项跳过。
void identity_fields_present(bool *sn, bool *key, bool *pk, bool *hw);

// 身份字段被 AT+CARDID= 改写后的通知。app 收到后重新组装并推送 BLE 身份 blob,
// 否则 …0012 特征会一直吐开机时的旧值,直到重启。
typedef void (*identity_changed_fn)(void *user);
void provision_console_set_changed_cb(identity_changed_fn cb, void *user);

// 启动 USB 串口产线/返修指令任务。须在 platform_create_identity 之后调用。
void provision_console_start(hal_identity_t *id);

// 产测命令回调:收到 AT+TEST 系列指令时调用(运行于串口任务上下文)。
// cb 负责执行对应测试并把应答文本写进 out(含结尾 \r\n),返回写入长度。
// out 已由框架预留 PROV_LINE_MAX 级别缓冲。cmd 见 services/factory_test.h。
#include "services/factory_test.h"
typedef int (*factory_test_fn)(ft_cmd_t cmd, char *out, int out_sz, void *user);
void provision_console_set_test_cb(factory_test_fn cb, void *user);

// FoloToy 风格分组 AT 指令回调:收到 at+config / at+command / at+reboot 时调用,
// line/len 为整行(未去大小写,值可能大小写敏感)。cb 解析并把应答写进 out
// (含 \r\n)返回写入长度。
typedef int (*at_cmd_fn)(const char *line, int len, char *out, int out_sz, void *user);
void provision_console_set_at_cb(at_cmd_fn cb, void *user);

// 注入给 core/services 的 token_bcast 的 HMAC 函数(签名与 token_mac_fn 一致)。
// user 传 hal_identity_t*。放在平台层是因为 core/services 不能依赖 mbedtls。
int token_mac_esp(const uint8_t *msg, int len, uint8_t out[8], void *user);

// 音频片段存储(SPIFFS 分区 "audio")。init 挂载(不存在则格式化)。
#include "services/audio_rx.h"
int  audio_store_init(void);
// 片段是否存在(纯 RAM 缓存,任意任务安全,含 NimBLE host)。
bool audio_store_clip_exists(int clip_id);
// 流式读片段:打开(校验头并跳过 12B)→ 逐块读 ADPCM → 关闭。整片不进 RAM(C3 无 PSRAM)。
// open 返回 0=成功 -1=不存在/损坏;read_chunk 返回读到的字节数(0=EOF)。仅供播放任务用(单读者)。
int  audio_store_clip_open(int clip_id);
int  audio_store_clip_read_chunk(uint8_t *buf, int max);
void audio_store_clip_close(void);
// 提供给 audio_clip_rx 的落盘 sink(写临时文件 → end 原子 rename)。
const audio_clip_sink_t *audio_store_sink(void);
// 触发播放本地片段(clip_id_t);始终有定义(音频不可用时为空实现)。
void audio_clip_play(int clip_id);
