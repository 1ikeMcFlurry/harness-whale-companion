// components/core/services/include/services/rtttl.h
#pragma once

typedef struct {
    const char *p, *end;
    int def_dur, def_oct, bpm, whole_ms;
} rtttl_t;

int rtttl_init(rtttl_t *r, const char *s, int len);
int rtttl_next(rtttl_t *r, int *freq_hz, int *ms);
