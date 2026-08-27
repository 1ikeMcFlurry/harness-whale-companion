#include "services/avatar_pack.h"

#include <stdbool.h>
#include <string.h>

#define HEADER_SIZE 16u
#define ENTRY_SIZE 28u

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static bool valid_name(const uint8_t raw[16], size_t *length) {
    size_t n = 0;
    while (n < 16 && raw[n] != 0) {
        uint8_t c = raw[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
        ++n;
    }
    if (n == 0 || n > 15 || raw[n] != 0) {
        return false;
    }
    for (size_t i = n + 1; i < 16; ++i) {
        if (raw[i] != 0) {
            return false;
        }
    }
    if (length != NULL) {
        *length = n;
    }
    return true;
}

static const uint8_t *entry_at(const uint8_t *base, uint8_t index) {
    return base + HEADER_SIZE + (size_t)index * ENTRY_SIZE;
}

int avatar_pack_init(avatar_pack_t *pack, const uint8_t *base, size_t size) {
    if (pack == NULL || base == NULL) {
        return AVATAR_PACK_ERR_ARG;
    }
    memset(pack, 0, sizeof(*pack));
    if (size < HEADER_SIZE || memcmp(base, "AVA1", 4) != 0 ||
        base[4] != 1 || base[5] == 0) {
        return AVATAR_PACK_ERR_FORMAT;
    }
    uint8_t count = base[5];
    uint16_t header_size = get16(base + 6);
    uint32_t total_size = get32(base + 8);
    size_t expected_header = HEADER_SIZE + (size_t)count * ENTRY_SIZE;
    if (header_size != expected_header || total_size < header_size ||
        total_size > size) {
        return AVATAR_PACK_ERR_BOUNDS;
    }
    if (crc32_bytes(base + HEADER_SIZE, expected_header - HEADER_SIZE) !=
        get32(base + 12)) {
        return AVATAR_PACK_ERR_CRC;
    }

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *entry = entry_at(base, i);
        if (!valid_name(entry, NULL)) {
            return AVATAR_PACK_ERR_FORMAT;
        }
        uint32_t offset = get32(entry + 16);
        uint32_t length = get32(entry + 20);
        if ((offset & 3u) != 0 || length == 0 || offset < header_size ||
            offset > total_size || length > total_size - offset) {
            return AVATAR_PACK_ERR_BOUNDS;
        }
        for (uint8_t j = 0; j < i; ++j) {
            const uint8_t *prior = entry_at(base, j);
            if (memcmp(entry, prior, 16) == 0) {
                return AVATAR_PACK_ERR_FORMAT;
            }
            uint32_t prior_offset = get32(prior + 16);
            uint32_t prior_length = get32(prior + 20);
            if (offset < prior_offset + prior_length &&
                prior_offset < offset + length) {
                return AVATAR_PACK_ERR_BOUNDS;
            }
        }
    }

    pack->base = base;
    pack->size = size;
    pack->header_size = header_size;
    pack->total_size = total_size;
    pack->count = count;
    return AVATAR_PACK_OK;
}

int avatar_pack_find(const avatar_pack_t *pack, const char *name,
                     const uint8_t **png, size_t *png_len) {
    if (pack == NULL || pack->base == NULL || name == NULL || png == NULL ||
        png_len == NULL) {
        return AVATAR_PACK_ERR_ARG;
    }
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 15) {
        return AVATAR_PACK_NOT_FOUND;
    }
    for (uint8_t i = 0; i < pack->count; ++i) {
        const uint8_t *entry = entry_at(pack->base, i);
        size_t entry_len = 0;
        if (!valid_name(entry, &entry_len)) {
            return AVATAR_PACK_ERR_FORMAT;
        }
        if (entry_len == name_len && memcmp(entry, name, name_len) == 0) {
            uint32_t offset = get32(entry + 16);
            uint32_t length = get32(entry + 20);
            const uint8_t *data = pack->base + offset;
            if (crc32_bytes(data, length) != get32(entry + 24)) {
                return AVATAR_PACK_ERR_CRC;
            }
            *png = data;
            *png_len = length;
            return AVATAR_PACK_OK;
        }
    }
    return AVATAR_PACK_NOT_FOUND;
}

int avatar_pack_first_name(const avatar_pack_t *pack, char name[16]) {
    return avatar_pack_name_at(pack, 0, name);
}

int avatar_pack_name_at(const avatar_pack_t *pack, uint8_t index, char name[16]) {
    if (pack == NULL || pack->base == NULL || index >= pack->count || name == NULL) {
        return AVATAR_PACK_ERR_ARG;
    }
    const uint8_t *entry = entry_at(pack->base, index);
    size_t length = 0;
    if (!valid_name(entry, &length)) {
        return AVATAR_PACK_ERR_FORMAT;
    }
    memcpy(name, entry, length);
    name[length] = '\0';
    return AVATAR_PACK_OK;
}
