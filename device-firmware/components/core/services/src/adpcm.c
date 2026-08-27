// components/core/services/src/adpcm.c —— IMA-ADPCM 单声道连续流解码(纯逻辑,无 FPU)
#include "services/adpcm.h"

static const int16_t STEP[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,
    107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,
    724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,
    3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
    15289,16818,18500,20350,22385,24623,27086,29794,32767
};
static const int8_t INDEX[16] = { -1,-1,-1,-1,2,4,6,8, -1,-1,-1,-1,2,4,6,8 };

static int16_t decode_code(adpcm_state_t *st, int code) {
    int step = STEP[st->index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    int pred = st->predictor;
    if (code & 8) pred -= diff; else pred += diff;
    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
    st->predictor = (int16_t)pred;
    int idx = st->index + INDEX[code & 0x0F];
    if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
    st->index = (int16_t)idx;
    return st->predictor;
}

void adpcm_state_reset(adpcm_state_t *st) { st->predictor = 0; st->index = 0; }

int adpcm_decode(adpcm_state_t *st, const uint8_t *in, int nbytes, int16_t *out) {
    int k = 0;
    for (int i = 0; i < nbytes; i++) {
        out[k++] = decode_code(st, in[i] & 0x0F);        // 低 nibble 先
        out[k++] = decode_code(st, (in[i] >> 4) & 0x0F); // 再高 nibble
    }
    return k;
}
