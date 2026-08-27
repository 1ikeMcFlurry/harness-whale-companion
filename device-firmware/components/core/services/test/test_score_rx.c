#include "unity.h"
#include "services/score_rx.h"
#include <string.h>

TEST_CASE("score_rx normal begin/data/end", "[scorerx]") {
    uint8_t buf[64]; score_rx_t r; score_rx_init(&r, buf, sizeof buf); int done;
    uint8_t begin[3]={0x00,4,0};
    TEST_ASSERT_EQUAL_INT(0, score_rx_frame(&r,begin,3,&done)); TEST_ASSERT_EQUAL_INT(0,done);
    uint8_t data[5]={0x01,'a','b','c','d'};
    TEST_ASSERT_EQUAL_INT(0, score_rx_frame(&r,data,5,&done)); TEST_ASSERT_EQUAL_INT(0,done);
    uint8_t end[1]={0x02};
    TEST_ASSERT_EQUAL_INT(0, score_rx_frame(&r,end,1,&done)); TEST_ASSERT_EQUAL_INT(1,done);
    TEST_ASSERT_EQUAL_UINT32(4, r.written);
    TEST_ASSERT_EQUAL_INT(0, memcmp(buf,"abcd",4));
}
TEST_CASE("score_rx data before begin -> 1", "[scorerx]") {
    uint8_t buf[64]; score_rx_t r; score_rx_init(&r, buf, sizeof buf); int done;
    uint8_t data[2]={0x01,'x'};
    TEST_ASSERT_EQUAL_INT(1, score_rx_frame(&r,data,2,&done));
}
TEST_CASE("score_rx end length mismatch -> 1", "[scorerx]") {
    uint8_t buf[64]; score_rx_t r; score_rx_init(&r, buf, sizeof buf); int done;
    uint8_t begin[3]={0x00,10,0}; score_rx_frame(&r,begin,3,&done);
    uint8_t data[3]={0x01,'a','b'}; score_rx_frame(&r,data,3,&done);
    uint8_t end[1]={0x02};
    TEST_ASSERT_EQUAL_INT(1, score_rx_frame(&r,end,1,&done)); TEST_ASSERT_EQUAL_INT(0,done);
}
TEST_CASE("score_rx begin oversize -> 3", "[scorerx]") {
    uint8_t buf[8]; score_rx_t r; score_rx_init(&r, buf, sizeof buf); int done;
    uint8_t begin[3]={0x00, 100, 0};
    TEST_ASSERT_EQUAL_INT(3, score_rx_frame(&r,begin,3,&done));
}
