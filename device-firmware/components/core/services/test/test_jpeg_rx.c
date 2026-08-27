#include "unity.h"
#include "services/jpeg_rx.h"
#include <string.h>

static uint8_t g_buf[1024];
static int g_written, g_ended, g_begin_ret, g_write_ret, g_end_ret;
static int m_begin(void*u,uint32_t t){(void)u;(void)t; g_written=0; return g_begin_ret;}
static int m_write(void*u,const uint8_t*d,int n){(void)u; memcpy(g_buf+g_written,d,n); g_written+=n; return g_write_ret;}
static int m_end(void*u){(void)u; g_ended=1; return g_end_ret;}
static jpeg_rx_sink_t SINK = { m_begin, m_write, m_end, NULL };
static void reset(void){ g_written=0; g_ended=0; g_begin_ret=0; g_write_ret=0; g_end_ret=0; }

TEST_CASE("jpeg_rx normal begin/data/end", "[jpegrx]") {
    reset(); jpeg_rx_t r; jpeg_rx_init(&r); int done;
    uint8_t begin[5]={0x00,4,0,0,0};
    TEST_ASSERT_EQUAL_INT(0, jpeg_rx_frame(&r,begin,5,&SINK,&done)); TEST_ASSERT_EQUAL_INT(0,done);
    uint8_t data[5]={0x01,'a','b','c','d'};
    TEST_ASSERT_EQUAL_INT(0, jpeg_rx_frame(&r,data,5,&SINK,&done)); TEST_ASSERT_EQUAL_INT(0,done);
    uint8_t end[1]={0x02};
    TEST_ASSERT_EQUAL_INT(0, jpeg_rx_frame(&r,end,1,&SINK,&done)); TEST_ASSERT_EQUAL_INT(1,done);
    TEST_ASSERT_EQUAL_INT(4,g_written); TEST_ASSERT_TRUE(g_ended);
    TEST_ASSERT_EQUAL_INT(0,memcmp(g_buf,"abcd",4));
}
TEST_CASE("jpeg_rx data/end before begin is sequence error", "[jpegrx]") {
    reset(); jpeg_rx_t r; jpeg_rx_init(&r); int done;
    uint8_t data[2]={0x01,'x'};
    TEST_ASSERT_EQUAL_INT(1, jpeg_rx_frame(&r,data,2,&SINK,&done));
    uint8_t end[1]={0x02};
    TEST_ASSERT_EQUAL_INT(1, jpeg_rx_frame(&r,end,1,&SINK,&done));
}
TEST_CASE("jpeg_rx end length mismatch → status 1, done 0", "[jpegrx]") {
    reset(); jpeg_rx_t r; jpeg_rx_init(&r); int done;
    uint8_t begin[5]={0x00,10,0,0,0};
    jpeg_rx_frame(&r,begin,5,&SINK,&done);
    uint8_t data[3]={0x01,'a','b'};
    jpeg_rx_frame(&r,data,3,&SINK,&done);
    uint8_t end[1]={0x02};
    TEST_ASSERT_EQUAL_INT(1, jpeg_rx_frame(&r,end,1,&SINK,&done)); TEST_ASSERT_EQUAL_INT(0,done);
}
TEST_CASE("jpeg_rx begin oversize returns 3", "[jpegrx]") {
    reset(); g_begin_ret=-1; jpeg_rx_t r; jpeg_rx_init(&r); int done;
    uint8_t begin[5]={0x00,0,0,2,0};
    TEST_ASSERT_EQUAL_INT(3, jpeg_rx_frame(&r,begin,5,&SINK,&done));
}

TEST_CASE("jpeg_rx init aborts an in-progress upload", "[jpegrx]") {
    reset(); jpeg_rx_t r; jpeg_rx_init(&r); int done;
    uint8_t begin[5]={0x00,4,0,0,0};
    uint8_t data[2]={0x01,'x'};
    uint8_t end[1]={0x02};

    TEST_ASSERT_EQUAL_INT(0, jpeg_rx_frame(&r,begin,5,&SINK,&done));
    TEST_ASSERT_TRUE(r.receiving);
    jpeg_rx_init(&r);
    TEST_ASSERT_FALSE(r.receiving);
    TEST_ASSERT_EQUAL_UINT32(0, r.total);
    TEST_ASSERT_EQUAL_UINT32(0, r.written);
    TEST_ASSERT_EQUAL_INT(1, jpeg_rx_frame(&r,data,2,&SINK,&done));
    TEST_ASSERT_EQUAL_INT(1, jpeg_rx_frame(&r,end,1,&SINK,&done));
}
