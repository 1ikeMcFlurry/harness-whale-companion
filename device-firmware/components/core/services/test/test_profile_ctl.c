#include "unity.h"
#include "services/profile_ctl.h"
#include <string.h>

TEST_CASE("profile default has new fields", "[profile]") {
    profile_data_t d; profile_ctl_init(&d);
    TEST_ASSERT_EQUAL_STRING("TraeWork", d.name);
    TEST_ASSERT_EQUAL_INT(0, d.token);
    TEST_ASSERT_EQUAL_INT(TOKEN_MAX_DEFAULT, d.token_max);
    TEST_ASSERT_EQUAL_INT(10, d.sleep_min);
    TEST_ASSERT_EQUAL_STRING("default", d.avatar_name);
}

TEST_CASE("profile serialize/deserialize round-trips", "[profile]") {
    profile_data_t a; profile_ctl_init(&a);
    a.level = 42; a.token = 1580; a.token_max = 50000; strcpy(a.name, "NEO");
    strcpy(a.avatar_name, "neon_cat");
    uint8_t blob[PROFILE_BLOB_SIZE];
    TEST_ASSERT_EQUAL_INT(PROFILE_BLOB_SIZE, profile_serialize(&a, blob, sizeof blob));
    profile_data_t b; memset(&b, 0, sizeof b);
    TEST_ASSERT_EQUAL_INT(0, profile_deserialize(&b, blob, PROFILE_BLOB_SIZE));
    TEST_ASSERT_EQUAL_INT(42, b.level);
    TEST_ASSERT_EQUAL_INT(1580, b.token);
    TEST_ASSERT_EQUAL_INT(50000, b.token_max);
    TEST_ASSERT_EQUAL_STRING("NEO", b.name);
    TEST_ASSERT_EQUAL_STRING("neon_cat", b.avatar_name);
}

TEST_CASE("profile migrates every v13 field and defaults avatar", "[profile]") {
    typedef struct {
        char name[48], role[40], subtitle[56];
        int battery, level, xp, xp_max;
        bool online;
        int token, token_max, sleep_min, game_total, game_best;
        uint8_t img_mode;
        int volume;
        bool pet_enabled;
        uint8_t pet_type;
    } profile_v13_fixture_t;
    profile_v13_fixture_t old = {0};
    strcpy(old.name, "OLD"); strcpy(old.role, "R"); strcpy(old.subtitle, "S");
    old.battery=71; old.level=8; old.xp=9; old.xp_max=10; old.online=true;
    old.token=11; old.token_max=12; old.sleep_min=13; old.game_total=14;
    old.game_best=15; old.img_mode=IMG_MODE_FULLSCREEN; old.volume=16;
    old.pet_enabled=true; old.pet_type=2;
    uint8_t blob[1 + sizeof old]; blob[0] = 13; memcpy(blob + 1, &old, sizeof old);
    profile_data_t d;
    TEST_ASSERT_EQUAL_INT(0, profile_deserialize(&d, blob, sizeof blob));
    TEST_ASSERT_EQUAL_STRING("OLD", d.name);
    TEST_ASSERT_EQUAL_STRING("R", d.role);
    TEST_ASSERT_EQUAL_STRING("S", d.subtitle);
    TEST_ASSERT_EQUAL_INT(71, d.battery); TEST_ASSERT_EQUAL_INT(8, d.level);
    TEST_ASSERT_EQUAL_INT(9, d.xp); TEST_ASSERT_EQUAL_INT(10, d.xp_max);
    TEST_ASSERT_TRUE(d.online); TEST_ASSERT_EQUAL_INT(11, d.token);
    TEST_ASSERT_EQUAL_INT(12, d.token_max); TEST_ASSERT_EQUAL_INT(13, d.sleep_min);
    TEST_ASSERT_EQUAL_INT(14, d.game_total); TEST_ASSERT_EQUAL_INT(15, d.game_best);
    TEST_ASSERT_EQUAL_INT(IMG_MODE_FULLSCREEN, d.img_mode);
    TEST_ASSERT_EQUAL_INT(16, d.volume); TEST_ASSERT_TRUE(d.pet_enabled);
    TEST_ASSERT_EQUAL_INT(2, d.pet_type);
    TEST_ASSERT_EQUAL_STRING("default", d.avatar_name);
}

TEST_CASE("profile deserialize rejects wrong version/len", "[profile]") {
    profile_data_t b; profile_ctl_init(&b);
    uint8_t blob[PROFILE_BLOB_SIZE]; profile_serialize(&b, blob, sizeof blob);
    blob[0] = 99;   // 坏版本
    TEST_ASSERT_EQUAL_INT(-1, profile_deserialize(&b, blob, PROFILE_BLOB_SIZE));
    TEST_ASSERT_EQUAL_INT(-1, profile_deserialize(&b, blob, PROFILE_BLOB_SIZE - 1));
}
