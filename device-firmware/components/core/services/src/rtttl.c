// components/core/services/src/rtttl.c —— RTTTL 解析(纯逻辑,无 FPU)
#include "services/rtttl.h"

static const int OCT4[12] = {262,277,294,311,330,349,370,392,415,440,466,494};

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int rd_int(const char **pp, const char *end) {
    int v = 0, any = 0; const char *p = *pp;
    while (p < end && is_digit(*p)) { v = v * 10 + (*p - '0'); p++; any = 1; }
    *pp = p; return any ? v : -1;
}
static int note_semi(char c) {
    switch (c) {
        case 'c': return 0; case 'd': return 2; case 'e': return 4; case 'f': return 5;
        case 'g': return 7; case 'a': return 9; case 'b': return 11; default: return -1;
    }
}

int rtttl_init(rtttl_t *r, const char *s, int len) {
    const char *end = s + len, *p = s;
    while (p < end && *p != ':') p++;
    if (p >= end) return -1;
    p++;
    r->def_dur = 4; r->def_oct = 6; r->bpm = 63;
    while (p < end && *p != ':') {
        while (p < end && (*p == ',' || *p == ' ')) p++;
        if (p >= end || *p == ':') break;
        char key = *p; p++;
        if (p < end && *p == '=') p++;
        int v = rd_int(&p, end);
        if (key == 'd' && v > 0) r->def_dur = v;
        else if (key == 'o' && v > 0) r->def_oct = v;
        else if (key == 'b' && v > 0) r->bpm = v;
    }
    if (p < end && *p == ':') p++;
    r->p = p; r->end = end;
    if (r->bpm <= 0) r->bpm = 63;
    r->whole_ms = 4 * 60000 / r->bpm;
    return 0;
}

int rtttl_next(rtttl_t *r, int *freq_hz, int *ms) {
    const char *p = r->p, *end = r->end;
    while (p < end && (*p == ',' || *p == ' ')) p++;
    if (p >= end) { r->p = p; return 0; }

    int dur = rd_int(&p, end);
    if (dur <= 0) dur = r->def_dur;
    if (p >= end) { r->p = p; return -1; }

    char nc = *p; if (nc >= 'A' && nc <= 'Z') nc += 32; p++;
    int dot = 0, freq;
    if (nc == 'p') {
        freq = 0;
        for (int k = 0; k < 2; k++) {
            if (p < end && *p == '.') { dot = 1; p++; }
            else if (p < end && is_digit(*p)) { rd_int(&p, end); }
            else break;
        }
    } else {
        int semi = note_semi(nc);
        if (semi < 0) { r->p = p; return -1; }
        if (p < end && *p == '#') { semi++; p++; }
        int oct = r->def_oct;
        for (int k = 0; k < 2; k++) {
            if (p < end && *p == '.') { dot = 1; p++; }
            else if (p < end && is_digit(*p)) { oct = rd_int(&p, end); }
            else break;
        }
        int base = OCT4[semi % 12];
        int oo = oct + semi / 12;
        if (oo > 4) base <<= (oo - 4);
        else if (oo < 4) base >>= (4 - oo);
        freq = base;
    }
    if (p < end && *p == '.') { dot = 1; p++; }

    int m = r->whole_ms / dur;
    if (dot) m = m * 3 / 2;
    *freq_hz = freq; *ms = m;
    r->p = p;
    return 1;
}
