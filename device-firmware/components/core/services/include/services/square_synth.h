// components/core/services/include/services/square_synth.h
#pragma once
#include <stdint.h>

void square_fill(int16_t *buf, int n, int freq_hz, int sr, int *phase, int amp);
