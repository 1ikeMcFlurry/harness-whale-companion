// components/platform/platform_esp32/src/png_decode.c —— 头像 PNG 流式解码 → ARGB8888 写 flash
// 用 ESP32-C3 ROM 里的 tinfl(miniz)做流式 inflate,自备 32KB 循环字典;逐扫描线去滤波、
// 转 ARGB8888、逐行写 imgframe。整图不进 RAM(只需字典+解压器+两行缓冲),但字典+解压器
// 约 43KB 是本机堆上的一次性峰值 —— 有内存守卫,不足则失败保留旧头像,绝不崩。
#include "platform/board_config.h"
#if PERIPH_DISPLAY
#include "png_decode.h"
#include "jpeg_store.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdlib.h>
#include <string.h>
#include "miniz.h"      // ROM: tinfl_decompress / tinfl_decompressor / tinfl_init

static const char *TAG = "png";

#define SCR_W 240
#define SCR_H 320
#define DICT_SZ 32768                 // tinfl 循环字典(deflate 最大回溯窗口)

// PNG 大端 32 位
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int paeth(int a, int b, int c) {   // PNG Paeth 预测
    int pp = a + b - c, pa = iabs(pp - a), pb = iabs(pp - b), pc = iabs(pp - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

// 扫描线组装/去滤波/转换 sink。inflate 产出的字节流按 [filter][stride 数据] 逐行喂入。
typedef struct {
    int w, h, bpp, stride;      // bpp=原始每像素字节(RGBA4/RGB3/灰1),stride=w*bpp
    int colortype;
    uint8_t *cur, *prev, *out;  // 当前行、上一行(去滤波用)、ARGB8888 输出行(w*4)
    int col;                    // 当前行已填数据字节 0..stride
    int has_filter, filter;     // 当前行是否已读到 filter 字节 / filter 类型
    int y;                      // 已完成的输出行号
    int err;                    // 写 flash 失败等
} sink_t;

static void defilter(sink_t *s) {
    uint8_t *c = s->cur, *p = s->prev; int bpp = s->bpp, n = s->stride;
    switch (s->filter) {
        case 0: break;                                              // None
        case 1: for (int i = bpp; i < n; i++) c[i] = (uint8_t)(c[i] + c[i - bpp]); break;             // Sub
        case 2: for (int i = 0; i < n; i++)   c[i] = (uint8_t)(c[i] + p[i]); break;                   // Up
        case 3: for (int i = 0; i < n; i++) {                                                          // Average
                    int a = i >= bpp ? c[i - bpp] : 0;
                    c[i] = (uint8_t)(c[i] + ((a + p[i]) >> 1));
                } break;
        case 4: for (int i = 0; i < n; i++) {                                                          // Paeth
                    int a = i >= bpp ? c[i - bpp] : 0;
                    int cc = i >= bpp ? p[i - bpp] : 0;
                    c[i] = (uint8_t)(c[i] + paeth(a, p[i], cc));
                } break;
        default: s->err = 1; break;
    }
}

// 原始行 → ARGB8888(LVGL 内存序 B,G,R,A),写入 imgframe 第 y 行。
static void convert_and_write(sink_t *s) {
    const uint8_t *c = s->cur; uint8_t *o = s->out; int w = s->w;
    if (s->colortype == 6) {                     // RGBA
        for (int x = 0; x < w; x++) { o[x*4+0]=c[x*4+2]; o[x*4+1]=c[x*4+1]; o[x*4+2]=c[x*4+0]; o[x*4+3]=c[x*4+3]; }
    } else if (s->colortype == 2) {              // RGB(不透明)
        for (int x = 0; x < w; x++) { o[x*4+0]=c[x*3+2]; o[x*4+1]=c[x*3+1]; o[x*4+2]=c[x*3+0]; o[x*4+3]=0xFF; }
    } else {                                     // 0=灰度(不透明)
        for (int x = 0; x < w; x++) { uint8_t g=c[x]; o[x*4+0]=g; o[x*4+1]=g; o[x*4+2]=g; o[x*4+3]=0xFF; }
    }
    if (jpeg_frame_write((uint32_t)s->y * w * 4, o, w * 4) != 0) s->err = 1;
}

// 消费一段解压出的字节:按 [filter][stride] 组装扫描线,满一行就去滤波+写出。
static void sink_bytes(sink_t *s, const uint8_t *d, int n) {
    while (n > 0 && !s->err && s->y < s->h) {
        if (!s->has_filter) { s->filter = *d++; n--; s->has_filter = 1; s->col = 0; continue; }
        int take = s->stride - s->col; if (take > n) take = n;
        memcpy(s->cur + s->col, d, (size_t)take); d += take; n -= take; s->col += take;
        if (s->col == s->stride) {
            defilter(s);
            convert_and_write(s);
            uint8_t *t = s->prev; s->prev = s->cur; s->cur = t;   // 交换:本行成为下一行的"上一行"
            s->y++; s->has_filter = 0; s->col = 0;
        }
    }
}

int png_decode_to_frame(const uint8_t *png, int len, int *ow, int *oh) {
    if (len < 8 || memcmp(png, "\x89PNG\r\n\x1a\n", 8) != 0) return -1;

    // ---- 解析 IHDR(必须是第一个块)----
    const uint8_t *p = png + 8, *end = png + len;
    if (p + 8 > end || be32(p) != 13 || memcmp(p + 4, "IHDR", 4) != 0) return -1;
    const uint8_t *ih = p + 8;
    int w = (int)be32(ih), h = (int)be32(ih + 4);
    int bitdepth = ih[8], colortype = ih[9], interlace = ih[12];
    if (bitdepth != 8) return -2;
    if (interlace != 0) return -3;
    int bpp = colortype == 6 ? 4 : colortype == 2 ? 3 : colortype == 0 ? 1 : -1;
    if (bpp < 0) return -4;                                   // 3=调色板等不支持
    if (w <= 0 || h <= 0 || w > SCR_W || h > SCR_H) return -5;
    uint32_t need = (uint32_t)w * h * 4;
    uint32_t cap = jpeg_frame_capacity();
    if (cap == 0 || need > cap) {
        ESP_LOGW(TAG, "PNG %dx%d 需 %uB 超 imgframe 容量 %uB,请让 pad 出更小的头像图",
                 w, h, (unsigned)need, (unsigned)cap);
        return -6;
    }

    // ---- 内存守卫:字典 32KB + 解压器 ~11KB + 三行缓冲,一次性峰值约 43KB ----
    size_t freeh = esp_get_free_heap_size();
    uint8_t *dict = malloc(DICT_SZ);
    tinfl_decompressor *dec = malloc(sizeof(tinfl_decompressor));
    uint8_t *b1 = calloc(1, (size_t)w * bpp);
    uint8_t *b2 = calloc(1, (size_t)w * bpp);   // prev 初值必须全 0(第一行 Up/Paeth 依赖)
    uint8_t *outrow = malloc((size_t)w * 4);
    if (!dict || !dec || !b1 || !b2 || !outrow) {
        ESP_LOGW(TAG, "PNG 解码内存不足(空闲堆 %uB),保留旧头像", (unsigned)freeh);
        free(dict); free(dec); free(b1); free(b2); free(outrow);
        return -7;
    }

    if (jpeg_frame_begin(need) != 0) {
        free(dict); free(dec); free(b1); free(b2); free(outrow);
        return -8;
    }

    sink_t sk = {0};
    sk.w = w; sk.h = h; sk.bpp = bpp; sk.stride = w * bpp; sk.colortype = colortype;
    sk.cur = b1; sk.prev = b2; sk.out = outrow;

    // ---- 先数一遍 IDAT 个数(为末块清 HAS_MORE_INPUT),再正式喂 ----
    int idat_total = 0;
    for (const uint8_t *q = p; q + 12 <= end; ) {
        uint32_t clen = be32(q);
        if (q + 12 + clen > end) break;
        if (memcmp(q + 4, "IDAT", 4) == 0) idat_total++;
        if (memcmp(q + 4, "IEND", 4) == 0) break;
        q += 12 + clen;
    }
    if (idat_total == 0) { free(dict); free(dec); free(b1); free(b2); free(outrow); return -9; }

    tinfl_init(dec);
    size_t out_ofs = 0;   // 循环字典写位置
    int idat_i = 0, done = 0, err = 0;
    for (const uint8_t *q = p; q + 12 <= end && !done && !err; ) {
        uint32_t clen = be32(q);
        if (q + 12 + clen > end) { err = 1; break; }
        if (memcmp(q + 4, "IEND", 4) == 0) break;
        if (memcmp(q + 4, "IDAT", 4) == 0) {
            const uint8_t *in = q + 8; size_t in_rem = clen;
            uint32_t flags = TINFL_FLAG_PARSE_ZLIB_HEADER |
                             (idat_i < idat_total - 1 ? TINFL_FLAG_HAS_MORE_INPUT : 0);
            idat_i++;
            for (;;) {
                size_t in_b = in_rem, out_b = DICT_SZ - out_ofs;
                tinfl_status st = tinfl_decompress(dec, in, &in_b, dict, dict + out_ofs, &out_b, flags);
                in += in_b; in_rem -= in_b;
                if (out_b) { sink_bytes(&sk, dict + out_ofs, (int)out_b); out_ofs = (out_ofs + out_b) & (DICT_SZ - 1); }
                if (sk.err) { err = 1; break; }
                if (st == TINFL_STATUS_HAS_MORE_OUTPUT) continue;     // 字典写满/回绕,继续
                if (st == TINFL_STATUS_DONE) { done = 1; break; }
                if (st == TINFL_STATUS_NEEDS_MORE_INPUT) break;       // 该块喂完,取下一 IDAT
                if (st < 0) { err = 1; break; }                       // 解压出错
            }
        }
        q += 12 + clen;
    }

    int rc = 0;
    if (err || sk.err)      { ESP_LOGW(TAG, "PNG 解码流出错(y=%d/%d)", sk.y, h); rc = -9; }
    else if (sk.y < h)      { ESP_LOGW(TAG, "PNG 数据不完整(y=%d/%d)", sk.y, h); rc = -9; }
    else { *ow = w; *oh = h; ESP_LOGI(TAG, "PNG 解码 %dx%d type=%d → ARGB8888 (空闲堆曾 %uB)",
                                      w, h, colortype, (unsigned)freeh); }

    free(dict); free(dec); free(b1); free(b2); free(outrow);
    return rc;
}
#endif // PERIPH_DISPLAY
