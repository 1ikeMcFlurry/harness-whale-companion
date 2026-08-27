// components/services/src/audio_service.c
#include "services/audio_service.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

void audio_service_init(audio_service_t *s, hal_audio_t *audio) {
    s->audio = audio;
    s->volume = 100;  // 兜底默认;实际以 profile.volume 为准(见 app.c 开机应用)
}

int audio_service_configure(audio_service_t *s, uint32_t hz, uint8_t bits, uint8_t ch) {
    if (!s->audio) return -1;
    int r = hal_audio_set_sample_rate(s->audio, hz, bits, ch);
    // codec open 会把输出音量重置为默认,这里重新应用当前音量,否则每次播放都变回默认。
    hal_audio_set_volume(s->audio, s->volume);
    return r;
}

void audio_service_set_volume(audio_service_t *s, uint8_t percent) {
    if (percent > 100) percent = 100;
    s->volume = percent;
    if (s->audio) hal_audio_set_volume(s->audio, percent);
}

int audio_service_play(audio_service_t *s, const void *pcm, size_t bytes) {
    if (!s->audio) return -1;
    size_t written = 0;
    return hal_audio_write(s->audio, pcm, bytes, &written);
}

int audio_service_record_playback(audio_service_t *s, uint32_t seconds) {
    if (!s->audio || seconds == 0) return -1;

    const uint32_t hz = 8000; const uint8_t bits = 16, ch = 1;   // 8kHz/16bit/单声道
    if (audio_service_configure(s, hz, bits, ch) != 0) return -2;

    const size_t total = (size_t)hz * (bits / 8) * ch * seconds; // 5s → 80000 字节
    uint8_t *buf = malloc(total);
    if (!buf) return -3;

    const size_t chunk = 2048;

    // 录音:分块读满整段
    for (size_t off = 0; off < total; ) {
        size_t want = (total - off) < chunk ? (total - off) : chunk;
        size_t got = 0;
        if (hal_audio_read(s->audio, buf + off, want, &got) != 0 || got == 0) {
            free(buf); return -4;
        }
        off += got;
    }

    // 回放:分块写回
    audio_service_set_volume(s, s->volume);
    for (size_t off = 0; off < total; ) {
        size_t want = (total - off) < chunk ? (total - off) : chunk;
        size_t wr = 0;
        if (hal_audio_write(s->audio, buf + off, want, &wr) != 0 || wr == 0) {
            free(buf); return -5;
        }
        off += wr;
    }

    free(buf);
    return 0;
}

// 声学自回环自检(产测用):边放 tone_hz 正弦音、边录麦克风(**不静音 DAC**),
// 用 Goertzel 算录到的信号在 tone_hz 处的能量。麦克风若能听到自己扬声器的音,
// 该频点能量就高 —— 一次证明"扬声器出声 + 麦克风收音"都好。纯电气串扰/环境噪声
// 不会在这个特定频点集中出能量,故比单纯测峰值更能区分真假。
//   out_peak     : 录音原始峰值幅度(诊断用,可为 NULL)
//   out_tone_mag : tone_hz 处的幅度(与峰值同量纲,0..32767;产测判据),可为 NULL
// 成功 0;-2 配置 / -3 内存 / -4 录音 / -5 播放。
int audio_service_loopback_test(audio_service_t *s, uint32_t tone_hz, uint32_t tone_ms,
                                int *out_peak, int *out_tone_mag) {
    if (out_peak) *out_peak = -1;
    if (out_tone_mag) *out_tone_mag = -1;
    if (!s->audio) return -1;

    const uint32_t hz = 8000; const uint8_t bits = 16, ch = 1;
    if (audio_service_configure(s, hz, bits, ch) != 0) return -2;
    audio_service_set_volume(s, s->volume);

    // 无缝循环音块:512 采样在 8kHz/1kHz 下正好 64 个整周期,首尾相接不断相位。
    const size_t CHUNK = 512;
    int16_t *tone = malloc(CHUNK * sizeof(int16_t));
    int16_t *mic  = malloc(CHUNK * sizeof(int16_t));
    if (!tone || !mic) { free(tone); free(mic); return -3; }
    const double amp = 2000.0;                        // 约 1/16 满幅:麦克风+ADC 增益高,
    // 用 8000 会把 ADC 打饱和(peak 削顶),降低驱动才能得到线性可比的 tone_mag。
    const float dphase = 2.0f * (float)M_PI * (float)tone_hz / (float)hz;
    float phase = 0.0f;
    for (size_t i = 0; i < CHUNK; i++) {
        tone[i] = (int16_t)(amp * sinf(phase));
        phase += dphase;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }

    // 冲掉全双工 RX 里积压的旧采样(约 0.25s),避免录到过期数据。
    for (size_t done = 0, flush = hz / 4; done < flush; ) {
        size_t g = 0;
        if (hal_audio_read(s->audio, mic, sizeof(int16_t) * CHUNK, &g) != 0 || g == 0) break;
        done += g / 2;
    }

    // Goertzel:单频点能量。coeff 只算一次 cosf,循环里只有乘加(C3 无 FPU 也扛得住)。
    const float coeff = 2.0f * cosf(dphase);
    float g1 = 0.0f, g2 = 0.0f;
    int peak = 0;
    long long sum = 0, sumsq = 0;
    size_t nsamp = 0;
    const size_t total = (size_t)hz * tone_ms / 1000;

    for (size_t off = 0; off < total; off += CHUNK) {
        size_t wr = 0;
        if (hal_audio_write(s->audio, tone, sizeof(int16_t) * CHUNK, &wr) != 0 || wr == 0) {
            free(tone); free(mic); return -5;
        }
        size_t got = 0;
        if (hal_audio_read(s->audio, mic, sizeof(int16_t) * CHUNK, &got) != 0 || got == 0) {
            free(tone); free(mic); return -4;
        }
        size_t m = got / 2;
        for (size_t i = 0; i < m; i++) {
            float x = (float)mic[i];
            float g0 = x + coeff * g1 - g2;
            g2 = g1; g1 = g0;
            int v = mic[i] < 0 ? -mic[i] : mic[i];
            if (v > peak) peak = v;
            sum += mic[i]; sumsq += (long long)mic[i] * mic[i];
        }
        nsamp += m;
    }

    // 音开:幅度 = 2*sqrt(power)/N(换算回采样量纲 0..32767)+ 直流均值 + rms。
    int mag = 0, mean = 0, rms = 0;
    if (nsamp > 0) {
        float power = g1 * g1 + g2 * g2 - coeff * g1 * g2;
        if (power < 0.0f) power = 0.0f;
        mag  = (int)(2.0f * sqrtf(power) / (float)nsamp);
        mean = (int)(sum / (long long)nsamp);
        rms  = (int)sqrtf((float)((double)sumsq / (double)nsamp));
    }

    // 静音基线:mute DAC 后录一小段,测同频点能量。与"音开"对比:差得越大,越说明
    // 该 1kHz 信号是"音一开才出现"(排除环境噪声/DC 偏置)。⚠ 仍无法区分信号来自
    // 声学(喇叭→空气→麦克风)还是共地电气串扰 —— 要区分请在真板上拔喇叭/断麦看 tone_mag 变化。
    hal_audio_set_out_mute(s->audio, true);
    for (size_t done = 0, flush = hz / 10; done < flush; ) {
        size_t g = 0;
        if (hal_audio_read(s->audio, mic, sizeof(int16_t) * CHUNK, &g) != 0 || g == 0) break;
        done += g / 2;
    }
    float b1 = 0.0f, b2 = 0.0f; int bpeak = 0; size_t bn = 0;
    for (size_t off = 0; off < hz / 6; off += CHUNK) {          // ~0.17s 基线
        size_t got = 0;
        if (hal_audio_read(s->audio, mic, sizeof(int16_t) * CHUNK, &got) != 0 || got == 0) break;
        size_t m = got / 2;
        for (size_t i = 0; i < m; i++) {
            float x = (float)mic[i];
            float g0 = x + coeff * b1 - b2; b2 = b1; b1 = g0;
            int v = mic[i] < 0 ? -mic[i] : mic[i];
            if (v > bpeak) bpeak = v;
        }
        bn += m;
    }
    hal_audio_set_out_mute(s->audio, false);
    int bmag = 0;
    if (bn > 0) {
        float bp = b1 * b1 + b2 * b2 - coeff * b1 * b2;
        if (bp < 0.0f) bp = 0.0f;
        bmag = (int)(2.0f * sqrtf(bp) / (float)bn);
    }

    if (out_peak) *out_peak = peak;
    if (out_tone_mag) *out_tone_mag = mag;
    printf("[audio] 回环 %uHz | 音开: tone_mag=%d peak=%d rms=%d dc=%d n=%u"
           " | 静音基线: mag=%d peak=%d n=%u | 音开/基线≈%d\n",
           (unsigned)tone_hz, mag, peak, rms, mean, (unsigned)nsamp,
           bmag, bpeak, (unsigned)bn, bmag > 0 ? mag / bmag : 999);

    free(tone); free(mic);
    return 0;
}

// 内部:分块把 buf 全部写出(回放)。成功 0,失败 -1。
static int play_all(audio_service_t *s, const uint8_t *buf, size_t total) {
    const size_t chunk = 2048;
    for (size_t off = 0; off < total; ) {
        size_t want = (total - off) < chunk ? (total - off) : chunk;
        size_t wr = 0;
        if (hal_audio_write(s->audio, buf + off, want, &wr) != 0 || wr == 0) return -1;
        off += wr;
    }
    return 0;
}

int audio_service_selftest(audio_service_t *s, uint32_t tone_hz, uint32_t tone_ms,
                           uint32_t rec_seconds, int *out_mic_peak) {
    if (out_mic_peak) *out_mic_peak = -1;
    if (!s->audio) return -1;

    const uint32_t hz = 8000; const uint8_t bits = 16, ch = 1;
    if (audio_service_configure(s, hz, bits, ch) != 0) return -2;  // 单次 open,读写共用
    audio_service_set_volume(s, s->volume);

    // 1) 输出通路测试:生成并播放正弦音(绕开麦克风)
    if (tone_ms > 0) {
        printf("[audio] >>> 播放测试音 %u Hz / %u ms(此时不用出声)...\n",
               (unsigned)tone_hz, (unsigned)tone_ms);
        size_t n = (size_t)hz * tone_ms / 1000;          // 采样点数
        int16_t *tone = malloc(n * sizeof(int16_t));
        if (!tone) return -3;
        const double amp = 8000.0;                        // 约 1/4 满幅,避免削顶
        // C3 无硬件 FPU、sinf 软模拟很慢;相位每周期取模保持在 [0,2π),
        // 避免大角度范围规约(__kernel_rem_pio2f),否则填充 2 万+采样会饿死看门狗。
        const float dphase = 2.0f * (float)M_PI * (float)tone_hz / (float)hz;
        float phase = 0.0f;
        for (size_t i = 0; i < n; i++) {
            tone[i] = (int16_t)(amp * sinf(phase));
            phase += dphase;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        int r = play_all(s, (uint8_t*)tone, n * sizeof(int16_t));
        free(tone);
        printf("[audio] <<< 测试音播放结束\n");
        if (r != 0) return -5;
    }

    // 2) 输入通路测试:录音并统计峰值幅度
    const size_t total = (size_t)hz * (bits / 8) * ch * rec_seconds;
    uint8_t *buf = malloc(total);
    if (!buf) return -3;

    const size_t chunk = 2048;

    // 录音期间静音 DAC 输出:全双工是 ADC/DAC 同时开、PA 常开,若不静音,
    // 扬声器会在录音时放出 DAC 空闲底噪,并可能经 PCB 共地耦合进模拟麦输入,
    // 把本就微弱的麦信号盖掉。录完、回放前再恢复。
    hal_audio_set_out_mute(s->audio, true);

    // 冲掉全双工 RX 在回放/空闲期间积压的旧采样(约 0.5s),
    // 否则下一轮录音先读到的是过期数据 → 录音内容与日志时间错位。
    {
        uint8_t junk[2048];
        size_t flush = (size_t)hz * (bits / 8) * ch / 2;   // ≈0.5s
        for (size_t done = 0; done < flush; ) {
            size_t want = (flush - done) < sizeof(junk) ? (flush - done) : sizeof(junk);
            size_t g = 0;
            if (hal_audio_read(s->audio, junk, want, &g) != 0 || g == 0) break;
            done += g;
        }
    }

    printf("[audio] ●●● 开始录音 %u 秒 —— 请现在对着麦克风说话/拍手!\n", (unsigned)rec_seconds);
    for (size_t off = 0; off < total; ) {
        size_t want = (total - off) < chunk ? (total - off) : chunk;
        size_t got = 0;
        if (hal_audio_read(s->audio, buf + off, want, &got) != 0 || got == 0) {
            hal_audio_set_out_mute(s->audio, false);
            free(buf); return -4;
        }
        off += got;
    }
    int peak = 0;
    const int16_t *sp = (const int16_t*)buf;
    const size_t nsamp = total / 2;
    long long sum = 0, sumsq = 0;
    size_t zc = 0;                       // 过零次数
    for (size_t i = 0; i < nsamp; i++) {
        int v = sp[i] < 0 ? -sp[i] : sp[i];
        if (v > peak) peak = v;
        sum += sp[i];
        sumsq += (long long)sp[i] * sp[i];
        if (i && ((sp[i] < 0) != (sp[i-1] < 0))) zc++;
    }
    int mean = (int)(sum / (long long)nsamp);
    int rms  = (int)sqrt((double)sumsq / (double)nsamp);
    int zcr  = (int)(100 * zc / (nsamp ? nsamp : 1));   // 过零率百分比
    if (out_mic_peak) *out_mic_peak = peak;
    printf("[audio] ■■■ 录音结束: peak=%d mean(DC)=%d rms=%d zcr=%d%%\n", peak, mean, rms, zcr);
    printf("[audio]     前16个采样: ");
    for (int i = 0; i < 16 && (size_t)i < nsamp; i++) printf("%d ", sp[i]);
    printf("\n");

    // 3) 回放录音(先恢复输出)
    hal_audio_set_out_mute(s->audio, false);
    printf("[audio] >>> 开始回放刚才的录音 %u 秒...\n", (unsigned)rec_seconds);
    int r = play_all(s, buf, total);
    free(buf);
    printf("[audio] <<< 回放结束\n");
    return r == 0 ? 0 : -5;
}
