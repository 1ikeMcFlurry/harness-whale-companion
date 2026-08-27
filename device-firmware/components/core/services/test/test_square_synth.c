#include "unity.h"
#include "services/square_synth.h"

TEST_CASE("square half-period + phase continuity", "[square]") {
    int16_t b[16]; int ph = 0;
    square_fill(b, 16, 1000, 8000, &ph, 6000);
    for (int i = 0; i < 16; i++) {
        int expect = ((i / 4) % 2 == 0) ? 6000 : -6000;
        TEST_ASSERT_EQUAL_INT16(expect, b[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, ph);
}
TEST_CASE("square rest is zero", "[square]") {
    int16_t b[8]; int ph = 5;
    square_fill(b, 8, 0, 8000, &ph, 6000);
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_INT16(0, b[i]);
    TEST_ASSERT_EQUAL_INT(0, ph);
}
