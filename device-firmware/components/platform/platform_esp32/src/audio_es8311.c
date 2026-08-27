// components/platform_esp32/src/audio_es8311.c
#include "platform/board_config.h"
#if PERIPH_AUDIO
#include "hal/hal_audio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"   // audio_codec_new_i2c_ctrl / audio_codec_new_i2s_data
#include "es8311_codec.h"             // es8311_codec_new / es8311_codec_cfg_t
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "audio_es8311";

// cur_*:当前 codec 已打开的格式。esp_codec_dev_open 在"已打开"时会直接返回 OK 且
// **不重新配置采样率**(见 esp_codec_dev.c 的 "Input already open" 分支),所以换格式
// 必须先 close。记录当前格式以便:同格式复用(不反复切换,免咔哒声)、异格式才重开。
typedef struct {
    esp_codec_dev_handle_t dev;
    i2s_chan_handle_t tx, rx;   // 留存:换采样率 close 后要补 enable(见 es_rate)
    uint32_t cur_hz;
    uint8_t  cur_bits, cur_ch;
    bool     opened;
} es_ctx_t;

static int es_write(hal_audio_t *self, const void *pcm, size_t n, size_t *w) {
    esp_codec_dev_handle_t d = ((es_ctx_t*)self->impl)->dev;
    if (!d) { if (w) *w = 0; return -1; }
    int r = esp_codec_dev_write(d, (void*)pcm, n);
    if (w) *w = (r == 0) ? n : 0;
    return r;
}
static int es_read(hal_audio_t *self, void *pcm, size_t n, size_t *got) {
    esp_codec_dev_handle_t d = ((es_ctx_t*)self->impl)->dev;
    if (!d) { if (got) *got = 0; return -1; }
    int r = esp_codec_dev_read(d, pcm, n);
    if (got) *got = (r == 0) ? n : 0;
    return r;
}
static void es_volume(hal_audio_t *self, uint8_t p) {
    esp_codec_dev_handle_t d = ((es_ctx_t*)self->impl)->dev;
    if (d) esp_codec_dev_set_out_vol(d, p);
}
static int es_out_mute(hal_audio_t *self, bool mute) {
    esp_codec_dev_handle_t d = ((es_ctx_t*)self->impl)->dev;
    if (!d) return -1;
    return esp_codec_dev_set_out_mute(d, mute);   // 硬静音 DAC(REG31)
}
// 打开 codec device 并设置输入增益。
// 关键:esp_codec_dev_open() 内部会调用 es8311 驱动的 es8311_config_sample(),
// 依据采样率与 MCLK 精确算出并写入所有时钟分频/OSR/BCLK 寄存器
// (REG01/02/03/04/05/06)以及数据格式。这里绝对不能在 open 之后再手动覆写
// 这些寄存器 —— 之前那样做会盖掉驱动算好的时钟分频,导致 ADC/DAC 采样时序
// 错乱,录音回放全是杂音。参考 folo 实现:open 之后只设增益,别碰寄存器。
static int es_rate(hal_audio_t *self, uint32_t hz, uint8_t bits, uint8_t ch) {
    es_ctx_t *c = (es_ctx_t*)self->impl;
    esp_codec_dev_handle_t d = c->dev;
    if (!d) return -1;
    // 已按同一格式打开 → 直接复用(反复 close/open 会有咔哒声且拖慢起播)。
    if (c->opened && c->cur_hz == hz && c->cur_bits == bits && c->cur_ch == ch) return 0;
    // 格式不同(如开机音 16k → 乐谱 8k):必须先 close,否则 open 见"已打开"直接返回,
    // 采样率不会被改,数据会以上一个速率播出(音调/速度错一倍)。
    if (c->opened) {
        esp_codec_dev_close(d);          // close 会把 I2S 通道 disable 回 READY
        c->opened = false;
        // 紧接着的 open 内部重配前又会无条件 disable 一次(audio_codec_data_i2s.c),
        // 而通道此刻已是 READY → 驱动打 "channel has not been enabled yet" ERROR。
        // 这里补一次 enable 让那次 disable 合法,消掉噪音日志(codec 未配,不出声)。
        if (c->tx) i2s_channel_enable(c->tx);
        if (c->rx) i2s_channel_enable(c->rx);
    }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = bits,
        .channel = ch,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = hz,
        .mclk_multiple = 0,   // 0 → 驱动按默认 256×fs 取 MCLK
    };
    int r = esp_codec_dev_open(d, &fs);
    if (r == 0) {
        // 驻极体麦模拟 PGA 增益。folo 参考取 30dB,电容麦常用起点。
        esp_codec_dev_set_in_gain(d, 30.0f);
        c->opened = true;
        c->cur_hz = hz; c->cur_bits = bits; c->cur_ch = ch;
        ESP_LOGD(TAG, "codec 打开 %uHz/%ubit/%uch", (unsigned)hz, bits, ch);
    } else {
        ESP_LOGE(TAG, "esp_codec_dev_open 失败: %d", r);
    }
    return r;
}

static const hal_audio_api_t API = {
    .write = es_write, .read = es_read, .set_volume = es_volume, .set_sample_rate = es_rate,
    .set_out_mute = es_out_mute,
};

// 全双工 I2S:同端口一 tx 一 rx,共用 MCLK/BCLK/WS,DOUT 播放、DIN 录音。
// 配置对齐 folo 参考:主模式、MCLK=256×fs、16bit 立体声 Philips 时序。
// 只 init_std_mode 配好引脚/slot,不在此 enable —— esp_codec_dev 在 open 时
// 会自行 reconfig_clock + enable 通道(见 audio_codec_data_i2s.c)。
static int i2s_full_duplex_init(const board_config_t *cfg, uint32_t sample_rate,
                                i2s_chan_handle_t *tx, i2s_chan_handle_t *rx) {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    esp_err_t e = i2s_new_channel(&chan_cfg, tx, rx);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2s_new_channel 失败: %d", e); return -1; }

    // 初始速率仅用于建通道;实际速率在 esp_codec_dev_open 时按需重配。
    i2s_std_config_t std = {
        .clk_cfg = {
            .sample_rate_hz = sample_rate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = cfg->i2s_mclk, .bclk = cfg->i2s_bclk, .ws = cfg->i2s_ws,
            .dout = cfg->i2s_dout, .din = cfg->i2s_din,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    if ((e = i2s_channel_init_std_mode(*tx, &std)) != ESP_OK) {
        ESP_LOGE(TAG, "i2s init tx 失败: %d", e); return -1;
    }
    if ((e = i2s_channel_init_std_mode(*rx, &std)) != ESP_OK) {
        ESP_LOGE(TAG, "i2s init rx 失败: %d", e); return -1;
    }
    // esp_codec_dev_open 内部重配前会先 i2s_channel_disable(见 audio_codec_data_i2s.c),
    // 而 disable 要求通道处于 RUNNING;首次 open 时通道刚 init(READY)未使能 → 驱动打印
    // "i2s_channel_disable: the channel has not been enabled yet"(无害,仅首次)。
    // 这里 init 后先 enable 一次,让首次 open 的 disable 合法 → 消掉该 ESP_LOGE。
    // 此刻 PA 未开、codec 未配,I2S 空转不出声(auto_clear 送零),无副作用。
    i2s_channel_enable(*tx);
    i2s_channel_enable(*rx);
    return 0;
}

#if I2C_BUS_SCAN_DEBUG
// I2C 总线扫描:逐个 7 位地址探测,打印所有 ACK 的设备,便于确认 ES8311
// 是否在线、接线是否正常。注意 ES8311_CODEC_DEFAULT_ADDR 是 8 位形式(0x30),
// 这里比对的是 7 位地址,故右移一位 → 0x18。
// 仅用于开机诊断,默认不编译(见 board_config.h 的 I2C_BUS_SCAN_DEBUG)。
static void i2c_bus_scan(i2c_master_bus_handle_t bus) {
    ESP_LOGI(TAG, "I2C 扫描开始(SDA/SCL 上的设备):");
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  发现设备 @ 0x%02X%s", addr,
                     addr == (ES8311_CODEC_DEFAULT_ADDR >> 1) ? "  <- ES8311" : "");
            found++;
        }
    }
    if (found == 0) ESP_LOGW(TAG, "  未发现任何 I2C 设备(检查接线/上拉/供电)");
    else            ESP_LOGI(TAG, "I2C 扫描完成,共 %d 个设备", found);
}
#endif // I2C_BUS_SCAN_DEBUG

// 工厂:i2c_master → es8311_codec_new → i2s_std(全双工) → esp_codec_dev_new。
// 任一步失败时 c->dev 留 NULL,HAL 读写会返回 -1(不崩)。
hal_audio_t *platform_create_audio(const board_config_t *cfg) {
    es_ctx_t *c = calloc(1, sizeof(*c));

    // 1) I2C master 总线(ES8311 控制口)
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = cfg->i2c_sda,
        .scl_io_num = cfg->i2c_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
#if I2C_BUS_SCAN_DEBUG
    // 加载 ES8311 驱动前扫描总线,确认 ES8311(0x18)/CW2017(0x63) 等是否在线(两者共用此总线)。
    // 用一条临时总线扫完即删:扫描对上百个空地址 probe 的 NACK 会污染 IDF i2c_master 的总线
    // 状态,若在正式总线上扫会导致随后 ES8311 读写失败;隔离到临时总线、扫完删除即可避开。
    {
        i2c_master_bus_handle_t scan_bus = NULL;
        if (i2c_new_master_bus(&i2c_cfg, &scan_bus) == ESP_OK) {
            i2c_bus_scan(scan_bus);
            i2c_del_master_bus(scan_bus);
        }
    }
#endif

    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t e = i2c_new_master_bus(&i2c_cfg, &i2c_bus);
    if (e != ESP_OK) { ESP_LOGE(TAG, "i2c_new_master_bus 失败: %d", e); goto done; }

    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    });
    if (!ctrl_if) { ESP_LOGE(TAG, "audio_codec_new_i2c_ctrl 失败"); goto done; }

    // 2) I2S 全双工数据口
    i2s_chan_handle_t tx = NULL, rx = NULL;
    if (i2s_full_duplex_init(cfg, 16000, &tx, &rx) != 0) goto done;
    c->tx = tx; c->rx = rx;   // 留存供 es_rate 换采样率时补 enable
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){
        .port = I2S_NUM_0, .tx_handle = tx, .rx_handle = rx,
    });
    if (!data_if) { ESP_LOGE(TAG, "audio_codec_new_i2s_data 失败"); goto done; }

    // 3) ES8311 codec 接口:ADC+DAC,外部 MCLK,PA 由 pa_ctrl 使能。
    //    hw_gain 对齐 folo 参考(PA 5V / DAC 3.3V),用于音量曲线换算。
    const audio_codec_if_t *codec_if = es8311_codec_new(&(es8311_codec_cfg_t){
        .ctrl_if     = ctrl_if,
        .gpio_if     = audio_codec_new_gpio(),
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin      = cfg->pa_ctrl,
        .pa_reverted = false,
        .master_mode = false,   // MCU I2S 为 master,codec 为 slave
        .use_mclk    = true,
        .hw_gain     = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
        // 单声道纯麦克风录音:必须 true,否则驱动写 REG44=0x58 进入 ADCL+DACR 参考模式,
        // 单声道读到的那一路是 DAC 参考(录音时静音)→ 录音恒为 0。true → REG44=0x08 纯 ADC。
        .no_dac_ref  = true,
    });
    if (!codec_if) { ESP_LOGE(TAG, "es8311_codec_new 失败"); goto done; }

    // 4) 组合成可读写的 codec device
    c->dev = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if  = data_if,
    });
    if (!c->dev) ESP_LOGE(TAG, "esp_codec_dev_new 失败");
    else         ESP_LOGI(TAG, "ES8311 初始化完成");

done:;
    hal_audio_t *a = calloc(1, sizeof(*a));
    a->api = &API; a->impl = c;
    return a;
}

// ================= 音频直通(麦克风→扬声器实时) =================
// 对着麦克风说话,扬声器同步放出:codec 只 open 一次,之后持续 read(ADC)→write(DAC)。
// 单声道 16k,小 static buffer 不占堆;不反复重配 codec(避免 I2C 偶发失败)。
// 进入后不返回,在自检任务里独占运行。
int platform_audio_passthrough(hal_audio_t *a) {
    if (!a) return -1;
    es_ctx_t *c = (es_ctx_t*)a->impl;
    esp_codec_dev_handle_t d = c->dev;
    if (!d) { ESP_LOGE(TAG, "直通:codec 未就绪"); return -1; }

    esp_codec_dev_close(d);
    c->opened = false;                     // 与 es_rate 的格式缓存保持一致
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0,
        .sample_rate = 16000, .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(d, &fs) != 0) { ESP_LOGE(TAG, "直通:open 失败"); return -2; }
    c->opened = true; c->cur_hz = 16000; c->cur_bits = 16; c->cur_ch = 1;
    esp_codec_dev_set_in_gain(d, 30.0f);   // 若啸叫就调低此值或下面音量
    esp_codec_dev_set_out_vol(d, 80);
    ESP_LOGI(TAG, "音频直通已启动:对着麦克风说话,扬声器实时输出");

    static int16_t buf[320];   // 20ms @16k 单声道,低延迟
    for (;;) {
        if (esp_codec_dev_read(d, buf, sizeof(buf)) == 0)
            esp_codec_dev_write(d, buf, sizeof(buf));
    }
    return 0;   // 不会到达
}
#endif // PERIPH_AUDIO
