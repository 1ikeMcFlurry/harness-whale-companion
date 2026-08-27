// components/services/include/services/audio_service.h
#pragma once
#include "hal/hal_audio.h"
#include <stdint.h>
#include <stddef.h>

// 音频业务:封装采样率/音量等配置,屏蔽具体 codec。
typedef struct {
    hal_audio_t *audio;
    uint8_t      volume;
} audio_service_t;

void audio_service_init(audio_service_t *, hal_audio_t *);
int  audio_service_configure(audio_service_t *, uint32_t hz, uint8_t bits, uint8_t ch);
void audio_service_set_volume(audio_service_t *, uint8_t percent);
int  audio_service_play(audio_service_t *, const void *pcm, size_t bytes);

// 自检:以 8kHz/16bit/单声道录音 `seconds` 秒到内存,再原样回放。
// 阻塞约 2*seconds 秒。成功返回 0;dev 未就绪 / 内存不足 / 读写失败返回负值。
int  audio_service_record_playback(audio_service_t *, uint32_t seconds);

// 诊断自检(单次 open 内完成,隔离输入/输出通路):
//   1) 生成并播放 tone_hz 正弦音 tone_ms 毫秒 —— 单独测输出(DAC→功放→喇叭)
//   2) 录音 rec_seconds 秒,统计录到的峰值幅度写入 *out_mic_peak —— 测输入(麦克风→ADC)
//   3) 原样回放录音
// 成功返回 0,失败返回负值(-2 配置/-3 内存/-4 录音/-5 回放)。out_mic_peak 可为 NULL。
int  audio_service_selftest(audio_service_t *, uint32_t tone_hz, uint32_t tone_ms,
                            uint32_t rec_seconds, int *out_mic_peak);

// 声学自回环自检(产测):边放 tone_hz 音、边录麦克风(不静音 DAC),用 Goertzel 测
// 录到信号在 tone_hz 处的能量。一次证明扬声器出声 + 麦克风收音,免人工。
//   out_peak     : 录音原始峰值(诊断用,可 NULL)
//   out_tone_mag : tone_hz 处幅度(0..32767,产测判据),可 NULL
// 成功 0;-2 配置 / -3 内存 / -4 录音 / -5 播放。约阻塞 tone_ms。
int  audio_service_loopback_test(audio_service_t *, uint32_t tone_hz, uint32_t tone_ms,
                                 int *out_peak, int *out_tone_mag);
