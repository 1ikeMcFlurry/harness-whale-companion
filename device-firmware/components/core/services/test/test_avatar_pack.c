#include <string.h>

#include "unity.h"
#include "services/avatar_pack.h"

#define FIXTURE_SIZE 160u

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t crc32_ref(const uint8_t *data, size_t len) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void make_fixture(uint8_t image[FIXTURE_SIZE]) {
    static const uint8_t png_a[] = {0x89, 'P', 'N', 'G', 1, 2, 3, 4};
    static const uint8_t png_b[] = {0x89, 'P', 'N', 'G', 5, 6, 7, 8};
    memset(image, 0xff, FIXTURE_SIZE);
    memcpy(image, "AVA1", 4);
    image[4] = 1;
    image[5] = 2;
    put16(image + 6, 72);
    put32(image + 8, 96);

    memset(image + 16, 0, 16);
    memcpy(image + 16, "default", 8);
    put32(image + 32, 80);
    put32(image + 36, sizeof(png_a));
    put32(image + 40, crc32_ref(png_a, sizeof(png_a)));
    memset(image + 44, 0, 16);
    memcpy(image + 44, "neon_cat", 9);
    put32(image + 60, 88);
    put32(image + 64, sizeof(png_b));
    put32(image + 68, crc32_ref(png_b, sizeof(png_b)));
    put32(image + 12, crc32_ref(image + 16, 56));
    memcpy(image + 80, png_a, sizeof(png_a));
    memcpy(image + 88, png_b, sizeof(png_b));
}

TEST_CASE("avatar pack initializes and finds named PNG", "[avatar_pack]") {
    uint8_t image[FIXTURE_SIZE];
    avatar_pack_t pack;
    const uint8_t *png = NULL;
    size_t len = 0;
    make_fixture(image);
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_OK, avatar_pack_find(&pack, "neon_cat", &png, &len));
    TEST_ASSERT_EQUAL_UINT32(8, len);
    TEST_ASSERT_EQUAL_MEMORY(image + 88, png, len);
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_NOT_FOUND,
                          avatar_pack_find(&pack, "missing", &png, &len));
}

TEST_CASE("avatar pack exposes first catalog name for boot fallback", "[avatar_pack]") {
    uint8_t image[FIXTURE_SIZE];
    avatar_pack_t pack;
    char name[16] = {0};
    make_fixture(image);
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_OK, avatar_pack_first_name(&pack, name));
    TEST_ASSERT_EQUAL_STRING("default", name);
}

TEST_CASE("avatar pack rejects malformed headers and index", "[avatar_pack]") {
    const size_t positions[] = {0, 4, 5, 6, 8, 12};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        uint8_t image[FIXTURE_SIZE];
        avatar_pack_t pack;
        make_fixture(image);
        image[positions[i]] ^= 0x55;
        TEST_ASSERT_NOT_EQUAL(AVATAR_PACK_OK,
                              avatar_pack_init(&pack, image, sizeof(image)));
    }
}

TEST_CASE("avatar pack rejects invalid duplicate overlapping and ranged entries", "[avatar_pack]") {
    uint8_t image[FIXTURE_SIZE];
    avatar_pack_t pack;

    make_fixture(image);
    memset(image + 16, 'x', 16);
    put32(image + 12, crc32_ref(image + 16, 56));
    TEST_ASSERT_NOT_EQUAL(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));

    make_fixture(image);
    memcpy(image + 44, image + 16, 16);
    put32(image + 12, crc32_ref(image + 16, 56));
    TEST_ASSERT_NOT_EQUAL(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));

    make_fixture(image);
    put32(image + 60, 84);
    put32(image + 12, crc32_ref(image + 16, 56));
    TEST_ASSERT_NOT_EQUAL(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));

    make_fixture(image);
    put32(image + 64, 1000);
    put32(image + 12, crc32_ref(image + 16, 56));
    TEST_ASSERT_NOT_EQUAL(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));
}

TEST_CASE("avatar lookup detects corrupt PNG and null arguments", "[avatar_pack]") {
    uint8_t image[FIXTURE_SIZE];
    avatar_pack_t pack;
    const uint8_t *png;
    size_t len;
    make_fixture(image);
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_OK, avatar_pack_init(&pack, image, sizeof(image)));
    image[88] ^= 1;
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_ERR_CRC,
                          avatar_pack_find(&pack, "neon_cat", &png, &len));
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_ERR_ARG, avatar_pack_init(NULL, image, sizeof(image)));
    TEST_ASSERT_EQUAL_INT(AVATAR_PACK_ERR_ARG, avatar_pack_find(&pack, NULL, &png, &len));
}
