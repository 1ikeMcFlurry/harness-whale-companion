// components/core/services/src/jpeg_probe.c
#include "services/jpeg_probe.h"
#include <stddef.h>

// 结尾往回找 EOI 的最大范围。有些编码器会在 FFD9 之后补几个字节的填充/EXIF 尾巴,
// 只看最后两字节会误判成截断。
#define EOI_SEARCH_TAIL 64

// SOF 家族里本解码器啃不动的:无损、差分、算术编码。
// 0xCC 是 DAC(定义算术编码表),它出现就说明整幅图是算术编码的。
static int is_unsupported_marker(uint8_t m) {
    switch (m) {
        case 0xC3:                          // SOF3  无损(顺序)
        case 0xC5: case 0xC6: case 0xC7:    // SOF5/6/7 差分
        case 0xC9: case 0xCA: case 0xCB:    // SOF9/10/11 算术编码
        case 0xCC:                          // DAC 算术编码表
        case 0xCD: case 0xCE: case 0xCF:    // SOF13/14/15 差分算术
            return 1;
        default:
            return 0;
    }
}

jpeg_probe_t jpeg_probe(const uint8_t *d, int len) {
    if (d == NULL || len < 4)                 return JPEG_PROBE_NOT_JPEG;
    if (d[0] != 0xFF || d[1] != 0xD8)         return JPEG_PROBE_NOT_JPEG;   // SOI

    int i = 2;
    int sos_found = 0;
    while (i + 1 < len) {
        if (d[i] != 0xFF) return JPEG_PROBE_NOT_JPEG;   // 段必须以 FF 开头
        while (i < len && d[i] == 0xFF) i++;            // 跳过填充 FF
        if (i >= len) return JPEG_PROBE_TRUNCATED;

        uint8_t m = d[i++];

        // 无长度字段的独立标记
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
        if (m == 0xD9) break;                            // EOI(SOS 之前就 EOI,空图)
        if (m == 0xDA) { sos_found = 1; break; }         // SOS 之后是熵编码数据,停止扫描

        if (m == 0xC2) return JPEG_PROBE_PROGRESSIVE;    // SOF2
        if (is_unsupported_marker(m)) return JPEG_PROBE_UNSUPPORTED;

        // 其余都是带 2 字节大端长度的段(含 SOF0/SOF1/DQT/DHT/APPn/COM…)
        if (i + 1 >= len) return JPEG_PROBE_TRUNCATED;
        int seglen = ((int)d[i] << 8) | (int)d[i + 1];
        if (seglen < 2) return JPEG_PROBE_NOT_JPEG;      // 长度字段自身含这 2 字节
        i += seglen;
    }

    if (!sos_found) return JPEG_PROBE_TRUNCATED;         // 扫到头也没见 SOS

    // 结尾必须有 EOI。熵编码数据里 FF 后面只可能跟 00(字节填充)或 D0~D7(RST),
    // 所以 FFD9 不会在图像数据中间出现,往回找是安全的。
    int tail = len - EOI_SEARCH_TAIL;
    if (tail < 2) tail = 2;
    for (int k = len - 2; k >= tail; k--)
        if (d[k] == 0xFF && d[k + 1] == 0xD9) return JPEG_PROBE_OK;

    return JPEG_PROBE_TRUNCATED;
}

const char *jpeg_probe_str(jpeg_probe_t r) {
    switch (r) {
        case JPEG_PROBE_OK:          return "基线 JPEG";
        case JPEG_PROBE_NOT_JPEG:    return "不是合法 JPEG(缺 SOI 或段结构错)";
        case JPEG_PROBE_PROGRESSIVE: return "渐进式 JPEG —— 本芯片只支持基线,请发送方重新编码";
        case JPEG_PROBE_UNSUPPORTED: return "算术编码/无损等变种,解码器不支持";
        case JPEG_PROBE_TRUNCATED:   return "JPEG 数据不完整(缺 SOS 或 EOI),传输可能被截断";
        default:                     return "未知";
    }
}
