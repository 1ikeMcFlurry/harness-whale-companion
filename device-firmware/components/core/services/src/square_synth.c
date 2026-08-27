// components/core/services/src/square_synth.c —— 整数方波合成(无 FPU)
#include "services/square_synth.h"

void square_fill(int16_t *buf, int n, int freq_hz, int sr, int *phase, int amp) {
    if (freq_hz <= 0 || sr <= 0) {
        for (int i = 0; i < n; i++) buf[i] = 0;
        *phase = 0;
        return;
    }
    // Q16 定点相位累加器:整周期=65536。避免 half=sr/(2*freq) 整数截断在高频造成严重音高偏差
    // (如 c6=1048Hz 用整数 half 会发成约 1333Hz,差 4 个半音)。inc=freq*65536/sr,精度足够。
    uint32_t inc = ((uint32_t)freq_hz << 16) / (uint32_t)sr;   // 每采样相位增量
    if (inc == 0) inc = 1;
    uint32_t ph = (uint32_t)*phase & 0xFFFFu;
    for (int i = 0; i < n; i++) {
        buf[i] = (ph & 0x8000u) ? (int16_t)(-amp) : (int16_t)amp;   // 相位后半周期取反
        ph = (ph + inc) & 0xFFFFu;
    }
    *phase = (int)ph;
}
