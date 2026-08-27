// components/platform_esp32/include/platform/board_config.h
#pragma once
#include <stdint.h>
#if defined(__has_include)
#  if __has_include("driver/spi_master.h")
#    include "driver/spi_master.h"
#  else
typedef int spi_host_device_t;
#    define SPI2_HOST 1
#  endif
#else
#  include "driver/spi_master.h"
#endif

// ===== 外设编译开关(1=包含该外设 / 0=不包含)=====
// 任意组合都应能编译通过。关闭某外设时:对应适配器源文件整段编译为空,
// 且组装层(app.c)不再创建/注入该外设。
#define PERIPH_DISPLAY  1   // ST7789P3 显示 + LVGL + 表现层
#define PERIPH_AUDIO    1   // ES8311 音频
#define PERIPH_BUTTON   1   // 按钮(接入 LVGL 导航:上/下/确定)
#define PERIPH_BLE      1   // BLE 档案卡控制
#define PERIPH_BATTERY  1   // 电池电量检测(CW2017 电量计,I2C)。总线上无芯片时自动退回虚拟电量

// This firmware image is a single-purpose DeepSeek Harness companion. Legacy
// passport, pet and game runtimes remain in the source tree for reference but
// are not initialized or exposed by the product runtime.
#define PRODUCT_HARNESS_ONLY 1

// ===== 屏幕整体圆角(适配外壳圆角开窗)=====
// 所有屏幕统一加此圆角 + 裁剪,圆角外露 4 角显示为黑边。改这一个值即可调整圆角半径。
// 单位:像素。0=不圆角(方屏)。典型 12~30,视外壳而定。
//
// ⚠ 内存代价(真机实测对照,半径 30 vs 0):整屏 clip_corner 需把子树先渲染到 layer buffer
//   再套遮罩 —— LVGL 池峰值 建界面时 +17KB、稳态 +9KB(35.4KB vs 26.9KB)。
//   故改动此项后必须重看日志的 lv_peak,并相应调 CONFIG_LV_MEM_SIZE_KILOBYTES
//   (见 sdkconfig.defaults)。当前 48KB 池就是按"半径 30 / 峰值 35.4KB"定的,余量约 24%。
#define SCREEN_CORNER_RADIUS  30

// ===== 电池电量检测:CW2017 电量计(3.7V 锂电,4.2V 满)=====
// I2C 从地址 0x63,**与 ES8311 共用 I2C_NUM_0 总线**(SDA=i2c_sda / SCL=i2c_scl,见下方 BOARD_CONFIG)。
// 电量计自带 FastCali,直接给 SOC%,无需分压电阻。芯片自带 Li-Poly profile 开箱即用。
#define BATT_SAMPLE_MS   3000     // 采样周期(ms);配合轻 EMA + 迟滞,电量稳、不突变
// 电池 profile(80 字节写入 CW2017)。profile 表见 src/cw2017_profiles.h,换电池只改下面两行:
#define CW2017_WRITE_PROFILE  1   // 1=按选定 profile 写芯片(默认);0=用芯片自带 Li-Poly profile
#define CW2017_PROFILE_SEL    1   // 选用 CW2017_PROFILES[] 里第几个 profile(1=4.2v_520mah)
// 电量检测关闭 / 总线上未检测到 CW2017 时,状态栏显示的虚拟占位电量
#define BATTERY_VIRTUAL_PCT   80  // 虚拟百分比
#define BATTERY_VIRTUAL_LEVEL 4   // 虚拟档位(1..5;80% 对应第 4 档)

// ===== 调试/测试开关(平时都置 0)=====
// 麦克风直通扬声器:置 1 后开机即把麦克风采集实时输出到扬声器(对着麦说话能听到自己),
// 用于排查 ES8311 输入/输出通路是否正常。
// 注意:该模式会独占音频且不返回,开启期间乐谱播放不可用(下发乐谱会回"存储失败"状态)。
#define AUDIO_MIC_PASSTHROUGH_TEST  0

// 游戏固定分调试:置 0 关闭;置 >0 时每局结束都按该固定分记账(如 100),
// 无视实际玩得多少。用于快速验证"积分累计 / BLE 读 …0013 / 达阈值给互动"整条同步链路,
// 不必真去玩到某个分数。仅影响 app.c 的每局结算(on_game_result),游戏本身照常玩。
#define GAME_DEBUG_FIXED_SCORE  0

// I2C 总线扫描:置 1 后在 ES8311 初始化前扫描总线,打印在线设备(ES8311=0x18 / CW2017=0x63)。
// 默认开启,便于确认音频/电量芯片是否在线。扫描在一条【临时总线】上进行、扫完即删,不污染
// 随后 ES8311 用的正式总线(NACK 风暴会残留总线状态,故必须隔离),因此常开也安全。
#define I2C_BUS_SCAN_DEBUG  0

// 临时深睡测试覆盖值(秒):>0 时忽略 standby_time，按该秒数进入深睡；量产前改回 0。
#define DEEP_SLEEP_TEST_SECONDS  0

// ===== 音频(本地片段,ADPCM)=====
#define AUDIO_ADPCM_SR       16000   // 片段播放采样率(16k 高质;片段写 flash 后离线播放)
#define AUDIO_SPIFFS_MOUNT   "/audio"
#define AUDIO_SPIFFS_LABEL   "audio"

// BLE 设备名
#define BLE_DEVICE_NAME "TRAE-CARD"

// 按钮数量(ADC 按键:上/下/确定 3 键,共用 GPIO0=ADC1_CH0)
#define BTN_COUNT 3

// 显示反色:1=开启反转(INVON 0x21),0=关闭(INVOFF 0x20)。
// ST7789P3(T200H7-C14-05)参考例程 TFT_init() 末尾无条件发送 0x21(INVON),
// 故默认开启;若换屏后颜色为负片,再改回 0。
#define LCD_INVERT_COLOR 1

typedef struct {
    // --- LCD (ST7789P3, 4-line SPI) ---
    uint16_t lcd_w, lcd_h;
    spi_host_device_t lcd_spi_host;
    int lcd_mosi, lcd_sclk, lcd_cs, lcd_dc, lcd_rst, lcd_bl;
    uint32_t lcd_pclk_hz;
    // --- Button (ADC 按键:共用 GPIO0=ADC1_CH0,外部 10k 上拉;分压 0/1k/2.2k = 上/下/确定) ---
    int btn_adc_channel;        // ADC1 通道号(GPIO0 = 0)
    int btn_wakeup_gpio;        // 深睡唤醒 GPIO；ADC 三键按下均将此节点拉为低电平
    int btn_gpio[BTN_COUNT];    // ADC 按键不使用,保留字段兼容旧代码
    uint8_t btn_active_level;   // ADC 按键不使用
    // --- Audio (ES8311) ---
    int i2c_sda, i2c_scl;
    int i2s_mclk, i2s_bclk, i2s_ws, i2s_dout, i2s_din, pa_ctrl;
} board_config_t;

// 占位引脚(请按实际原理图修改)。ESP32-C3 仅有 GPIO 0..21,且 GPIO 11..17 通常用于
// 板载 SPI flash。下面优先保证"显示+LED+按钮"占用 0..10 各不冲突;C3 引脚总数有限,
// 同时启用音频(I2C/I2S 共 8 脚)会与显示引脚重叠,接音频时需重新规划引脚。
// lcd_rst = -1 表示复位脚不接 MCU(硬接 3.3V),改由软件复位命令初始化。
// 复合字面量中未列出的字段默认为 0;其余外设引脚见下方示例,需要时再填。
#define BOARD_CONFIG_DEFAULT() (board_config_t){              \
    /* --- LCD ST7789P3 240x320, 4-line SPI --- */           \
    .lcd_w = 240, .lcd_h = 320,                               \
    .lcd_spi_host = SPI2_HOST,                                \
    .lcd_mosi = 9, .lcd_sclk = 8, .lcd_cs = 1,               \
    .lcd_dc = 20, .lcd_rst = -1, .lcd_bl = 21,               \
    /* 40MHz leaves ample headroom for 60Hz Tama dirty-region updates and  */ \
    /* is substantially more tolerant of FPC/board signal integrity.       */ \
    .lcd_pclk_hz = 40 * 1000 * 1000,                          \
    /* --- Button: ADC 按键,GPIO0 = ADC1_CH0 --- */          \
    .btn_adc_channel = 0,                                     \
    .btn_wakeup_gpio = 0,                                    \
    /* --- Audio ES8311(已调通,勿动) --- */                  \
    .i2c_sda = 10, .i2c_scl = 7,                              \
    .i2s_mclk = 6, .i2s_bclk = 5, .i2s_ws = 3,                \
    .i2s_dout = 2, .i2s_din = 4, .pa_ctrl = -1,               \
}
// 其余外设引脚示例(默认 0;若启用对应 PERIPH_* 请按原理图填写并取消注释):
//   .i2c_sda = 18, .i2c_scl = 19,
//   .i2s_mclk = 20, .i2s_bclk = 21, .i2s_ws = 9,
//   .i2s_dout = 10, .i2s_din = 5, .pa_ctrl = 4,
