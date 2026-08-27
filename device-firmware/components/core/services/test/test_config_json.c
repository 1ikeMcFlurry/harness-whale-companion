#include "unity.h"
#include "services/config_json.h"
#include <string.h>

TEST_CASE("json partial update changes only present keys", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    const char *j = "{\"name\":\"NEO\",\"level\":7,\"token\":1580}";
    cfg_changed_t c;
    int n = config_json_apply(&d, j, (int)strlen(j), &c);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("NEO", d.name);
    TEST_ASSERT_EQUAL_INT(7, d.level);
    TEST_ASSERT_EQUAL_INT(1580, d.token);
    TEST_ASSERT_TRUE(c.name && c.level && c.token);
    TEST_ASSERT_FALSE(c.role);
    TEST_ASSERT_EQUAL_STRING("NETRUNNER", d.role);
}

TEST_CASE("json parses token_max and clamps to >=1", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    cfg_changed_t c;
    const char *j = "{\"token\":100,\"token_max\":8888}";
    int n = config_json_apply(&d, j, (int)strlen(j), &c);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_TRUE(c.token && c.token_max);
    TEST_ASSERT_EQUAL_INT(100, d.token);
    TEST_ASSERT_EQUAL_INT(8888, d.token_max);
    const char *j2 = "{\"token_max\":0}";
    config_json_apply(&d, j2, (int)strlen(j2), NULL);
    TEST_ASSERT_EQUAL_INT(1, d.token_max);
}

TEST_CASE("json clamps out-of-range numbers", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    const char *j = "{\"battery\":250,\"level\":-5,\"xp_max\":0,\"token\":-9}";
    int n = config_json_apply(&d, j, (int)strlen(j), NULL);
    TEST_ASSERT_TRUE(n >= 3);
    TEST_ASSERT_EQUAL_INT(100, d.battery);
    TEST_ASSERT_EQUAL_INT(0, d.level);
    TEST_ASSERT_EQUAL_INT(1, d.xp_max);
    TEST_ASSERT_EQUAL_INT(0, d.token);
}

TEST_CASE("json wrong-type key is skipped", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    const char *j = "{\"battery\":\"high\",\"online\":true}";
    cfg_changed_t c;
    int n = config_json_apply(&d, j, (int)strlen(j), &c);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_FALSE(c.battery);
    TEST_ASSERT_TRUE(c.online);
    TEST_ASSERT_EQUAL_INT(82, d.battery);
}

TEST_CASE("json no known key returns 0", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    const char *j = "{\"unknown\":1}";
    TEST_ASSERT_EQUAL_INT(0, config_json_apply(&d, j, (int)strlen(j), NULL));
}

TEST_CASE("json bad or non-object returns -1", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    TEST_ASSERT_EQUAL_INT(-1, config_json_apply(&d, "not json", 8, NULL));
    TEST_ASSERT_EQUAL_INT(-1, config_json_apply(&d, "[1,2]", 5, NULL));
}

TEST_CASE("json nickname overrides legacy name and is UTF8 safe", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d);
    const char *j = "{\"name\":\"legacy\",\"nickname\":\"昵称\"}";
    cfg_changed_t c;
    TEST_ASSERT_EQUAL_INT(1, config_json_apply(&d, j, (int)strlen(j), &c));
    TEST_ASSERT_EQUAL_STRING("昵称", d.name);
    TEST_ASSERT_TRUE(c.name);
}

TEST_CASE("json accepts bounded avatar names and skips invalid ones", "[cfgjson]") {
    profile_data_t d; profile_ctl_init(&d); cfg_changed_t c;
    const char *good = "{\"avatar_name\":\"violet_owl\"}";
    TEST_ASSERT_EQUAL_INT(1, config_json_apply(&d, good, (int)strlen(good), &c));
    TEST_ASSERT_TRUE(c.avatar_name);
    TEST_ASSERT_EQUAL_STRING("violet_owl", d.avatar_name);
    const char *bad = "{\"avatar_name\":\"Bad Avatar Name\"}";
    TEST_ASSERT_EQUAL_INT(0, config_json_apply(&d, bad, (int)strlen(bad), &c));
    TEST_ASSERT_FALSE(c.avatar_name);
    TEST_ASSERT_EQUAL_STRING("violet_owl", d.avatar_name);
}
