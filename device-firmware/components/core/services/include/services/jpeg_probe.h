// components/core/services/include/services/jpeg_probe.h —— JPEG 兼容性预检(纯逻辑)
#pragma once
#include <stdint.h>

// 解码前先扫一遍段标记,把"解码器一定啃不动"的情况提前识别出来。
//
// 动机:esp_new_jpeg 只支持基线 JPEG(见其 README FAQ)。喂给它渐进式图时,
// 头部能解析出宽高、尺寸检查也过,直到第一次 jpeg_dec_process 才在 SOS 段失败,
// 返回一个笼统的错误码。上层只能回报"解码失败",小程序开发者根本猜不到原因。
// 提前探测就能给出确切的失败理由。
typedef enum {
    JPEG_PROBE_OK = 0,         // 基线,可以交给解码器
    JPEG_PROBE_NOT_JPEG,       // 没有 SOI,或段结构非法
    JPEG_PROBE_PROGRESSIVE,    // SOF2 渐进式 —— 本芯片无法支持,见下
    JPEG_PROBE_UNSUPPORTED,    // 算术编码 / 无损 / 差分等冷门变种
    JPEG_PROBE_TRUNCATED,      // 数据不完整(扫到头还没见 SOS,或结尾缺 EOI)
} jpeg_probe_t;

// d/len = 完整的 JPEG 字节流。函数只读,不修改。
jpeg_probe_t jpeg_probe(const uint8_t *d, int len);

// 给日志和回报用的短说明(中文)。
const char *jpeg_probe_str(jpeg_probe_t r);
