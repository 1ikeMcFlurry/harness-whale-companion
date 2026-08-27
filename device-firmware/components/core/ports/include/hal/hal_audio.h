// components/hal/include/hal/hal_audio.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct hal_audio_s hal_audio_t;

typedef struct {
    int  (*write)(hal_audio_t *self, const void *pcm, size_t n, size_t *written);
    int  (*read) (hal_audio_t *self, void *pcm, size_t n, size_t *got);
    void (*set_volume)(hal_audio_t *self, uint8_t percent);
    int  (*set_sample_rate)(hal_audio_t *self, uint32_t hz, uint8_t bits, uint8_t ch);
    int  (*set_out_mute)(hal_audio_t *self, bool mute);   // 可选,NULL 表示不支持
} hal_audio_api_t;

struct hal_audio_s { const hal_audio_api_t *api; void *impl; };

static inline int hal_audio_write(hal_audio_t *a, const void *p, size_t n, size_t *w) {
    return a->api->write(a, p, n, w);
}
static inline int hal_audio_read(hal_audio_t *a, void *p, size_t n, size_t *g) {
    return a->api->read(a, p, n, g);
}
static inline void hal_audio_set_volume(hal_audio_t *a, uint8_t percent) {
    a->api->set_volume(a, percent);
}
static inline int hal_audio_set_sample_rate(hal_audio_t *a, uint32_t hz, uint8_t bits, uint8_t ch) {
    return a->api->set_sample_rate(a, hz, bits, ch);
}
// 静音/取消静音输出(DAC)。实现可选:未实现时返回 0(视为无操作)。
static inline int hal_audio_set_out_mute(hal_audio_t *a, bool mute) {
    return a->api->set_out_mute ? a->api->set_out_mute(a, mute) : 0;
}
