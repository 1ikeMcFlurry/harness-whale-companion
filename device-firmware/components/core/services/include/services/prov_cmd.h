// components/core/services/include/services/prov_cmd.h —— 产线/返修 AT 指令解析(纯逻辑)
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define PROV_KEY_MAX 32
#define PROV_PK_MAX  16
#define PROV_HW_MAX  8

typedef enum {
    PROV_CMD_NONE = 0,   // 空行 / 无法识别
    PROV_CMD_SET,        // AT+CARDID=key=..[,pk=..][,hw=..]
    PROV_CMD_QUERY,      // AT+CARDID?
} prov_kind_t;

typedef enum {
    PROV_ERR_NONE = 0,
    PROV_ERR_UNKNOWN,         // 不认识的指令
    PROV_ERR_MISSING_PARAM,   // 一个可写字段都没给,或某字段值为空
    PROV_ERR_TOO_LONG,        // 某字段值超长
    PROV_ERR_SN_READONLY,     // 传了 sn= —— SN 由 MAC 决定,运行时不可改
    PROV_ERR_BAD_CHAR,        // 值里含不可打印字符(如退格键)。串口不回显 key,
                              // 不在这里拦住的话操作员永远发现不了自己打错了
} prov_err_t;

typedef struct {
    prov_kind_t kind;
    prov_err_t  err;
    bool     has_key, has_pk, has_hw;
    char     key[PROV_KEY_MAX + 1];
    char     pk [PROV_PK_MAX  + 1];
    char     hw [PROV_HW_MAX  + 1];
} prov_cmd_t;

// 解析一行(可含结尾 CR/LF,会被去掉)。out 必填,函数内部先清零。
// 指令前缀不区分大小写;**参数值原样保留大小写**(Key 大小写敏感)。
void prov_parse(const char *line, int len, prov_cmd_t *out);

// 错误码 → 回给串口的字符串(用于 "+ERR=<原因>")。
const char *prov_err_str(prov_err_t e);
