#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *base;
    size_t size;
    uint16_t header_size;
    uint32_t total_size;
    uint8_t count;
} avatar_pack_t;

enum {
    AVATAR_PACK_OK = 0,
    AVATAR_PACK_NOT_FOUND = 1,
    AVATAR_PACK_ERR_ARG = -1,
    AVATAR_PACK_ERR_FORMAT = -2,
    AVATAR_PACK_ERR_BOUNDS = -3,
    AVATAR_PACK_ERR_CRC = -4,
};

int avatar_pack_init(avatar_pack_t *pack, const uint8_t *base, size_t size);
int avatar_pack_find(const avatar_pack_t *pack, const char *name,
                     const uint8_t **png, size_t *png_len);
int avatar_pack_first_name(const avatar_pack_t *pack, char name[16]);
// index 为 0-based 构建目录顺序。广播协议的 avatar_id 为 1-based,调用方须减 1。
int avatar_pack_name_at(const avatar_pack_t *pack, uint8_t index, char name[16]);

#ifdef __cplusplus
}
#endif
