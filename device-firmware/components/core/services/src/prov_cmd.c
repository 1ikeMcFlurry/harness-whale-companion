// components/core/services/src/prov_cmd.c
#include "services/prov_cmd.h"
#include <string.h>

// 大小写不敏感的前缀比较。s 长度 len,pfx 以 '\0' 结尾。
static int ci_prefix(const char *s, int len, const char *pfx) {
    int n = (int)strlen(pfx);
    if (len < n) return 0;
    for (int i = 0; i < n; i++) {
        char a = s[i], b = pfx[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

// 键名精确匹配(长度也要等),大小写不敏感。
static int key_is(const char *k, int klen, const char *name) {
    return klen == (int)strlen(name) && ci_prefix(k, klen, name);
}

// 返回 0=空值(missing) / -1=超长 / -2=含不可打印字符 / 1=成功。**原样拷贝,不改大小写。**
static int copy_val(char *dst, int cap, const char *v, int vlen) {
    if (vlen <= 0)   return 0;
    if (vlen > cap)  return -1;
    for (int i = 0; i < vlen; i++) {
        // 只接受 ASCII 可见字符。退格/控制字符/空格一律拒绝 ——
        // 串口不回显 key,这里不拦就再也没有第二道防线了。
        if (v[i] < 0x21 || v[i] > 0x7E) return -2;
    }
    memcpy(dst, v, (size_t)vlen);
    dst[vlen] = '\0';
    return 1;
}

void prov_parse(const char *line, int len, prov_cmd_t *out) {
    memset(out, 0, sizeof *out);
    if (line == NULL) { out->err = PROV_ERR_UNKNOWN; return; }

    // 去首尾空白与 CR/LF
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                       line[len - 1] == ' '  || line[len - 1] == '\t')) len--;
    while (len > 0 && (*line == ' ' || *line == '\t')) { line++; len--; }
    if (len == 0) { out->kind = PROV_CMD_NONE; return; }

    if (len == 10 && ci_prefix(line, len, "AT+CARDID?")) {
        out->kind = PROV_CMD_QUERY;
        return;
    }
    if (!ci_prefix(line, len, "AT+CARDID=")) {
        out->kind = PROV_CMD_NONE;
        out->err  = PROV_ERR_UNKNOWN;
        return;
    }

    out->kind = PROV_CMD_SET;
    const char *p = line + 10, *end = line + len;
    while (p < end) {
        const char *seg_end = (const char *)memchr(p, ',', (size_t)(end - p));
        if (seg_end == NULL) seg_end = end;
        const char *eq = (const char *)memchr(p, '=', (size_t)(seg_end - p));
        if (eq != NULL) {
            int         klen = (int)(eq - p);
            const char *v    = eq + 1;
            int         vlen = (int)(seg_end - v);
            int r = 1;
            if (key_is(p, klen, "sn")) {
                // SN 由 MAC 决定。报错而不是静默忽略 —— 静默会让操作员
                // 以为改成功了,直到后面绑定对不上才发现。
                out->err = PROV_ERR_SN_READONLY;
                return;
            } else if (key_is(p, klen, "key")) {
                r = copy_val(out->key, PROV_KEY_MAX, v, vlen);
                if (r == 1) out->has_key = true;
            } else if (key_is(p, klen, "pk")) {
                r = copy_val(out->pk, PROV_PK_MAX, v, vlen);
                if (r == 1) out->has_pk = true;
            } else if (key_is(p, klen, "hw")) {
                r = copy_val(out->hw, PROV_HW_MAX, v, vlen);
                if (r == 1) out->has_hw = true;
            }
            // 未知键静默忽略(向后兼容将来新增字段)
            if (r == 0)  { out->err = PROV_ERR_MISSING_PARAM; return; }
            if (r == -1) { out->err = PROV_ERR_TOO_LONG;      return; }
            if (r == -2) { out->err = PROV_ERR_BAD_CHAR;      return; }
        }
        p = (seg_end < end) ? seg_end + 1 : end;
    }
    if (!out->has_key && !out->has_pk && !out->has_hw) out->err = PROV_ERR_MISSING_PARAM;
}

const char *prov_err_str(prov_err_t e) {
    switch (e) {
        case PROV_ERR_NONE:          return "none";
        case PROV_ERR_MISSING_PARAM: return "missing_param";
        case PROV_ERR_TOO_LONG:      return "too_long";
        case PROV_ERR_SN_READONLY:   return "sn_readonly";
        case PROV_ERR_BAD_CHAR:      return "bad_char";
        default:                     return "unknown";
    }
}
