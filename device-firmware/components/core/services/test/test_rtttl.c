#include "unity.h"
#include "services/rtttl.h"
#include <string.h>

TEST_CASE("rtttl header defaults + whole_ms", "[rtttl]") {
    rtttl_t r; const char *s = "x:d=8,o=5,b=120:c";
    TEST_ASSERT_EQUAL_INT(0, rtttl_init(&r, s, (int)strlen(s)));
    TEST_ASSERT_EQUAL_INT(8, r.def_dur);
    TEST_ASSERT_EQUAL_INT(5, r.def_oct);
    TEST_ASSERT_EQUAL_INT(120, r.bpm);
    TEST_ASSERT_EQUAL_INT(2000, r.whole_ms);
}
TEST_CASE("rtttl notes freq/ms + rest", "[rtttl]") {
    rtttl_t r; const char *s = "x:d=4,o=5,b=100:8c6,4a,p";
    TEST_ASSERT_EQUAL_INT(0, rtttl_init(&r, s, (int)strlen(s)));
    int f, m;
    TEST_ASSERT_EQUAL_INT(1, rtttl_next(&r, &f, &m));
    TEST_ASSERT_EQUAL_INT(1048, f); TEST_ASSERT_EQUAL_INT(300, m);
    TEST_ASSERT_EQUAL_INT(1, rtttl_next(&r, &f, &m));
    TEST_ASSERT_EQUAL_INT(880, f);  TEST_ASSERT_EQUAL_INT(600, m);
    TEST_ASSERT_EQUAL_INT(1, rtttl_next(&r, &f, &m));
    TEST_ASSERT_EQUAL_INT(0, f);    TEST_ASSERT_EQUAL_INT(600, m);
    TEST_ASSERT_EQUAL_INT(0, rtttl_next(&r, &f, &m));
}
TEST_CASE("rtttl sharp and dotted", "[rtttl]") {
    rtttl_t r; const char *s = "x:d=4,o=5,b=120:c#6,8a.";
    rtttl_init(&r, s, (int)strlen(s));
    int f, m;
    rtttl_next(&r, &f, &m);
    TEST_ASSERT_EQUAL_INT(1108, f);
    TEST_ASSERT_EQUAL_INT(500, m);
    rtttl_next(&r, &f, &m);
    TEST_ASSERT_EQUAL_INT(880, f);
    TEST_ASSERT_EQUAL_INT(375, m);
}
