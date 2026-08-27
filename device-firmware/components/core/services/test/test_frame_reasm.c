#include "unity.h"
#include "services/frame_reasm.h"
#include <string.h>

static void mk_hdr(uint8_t *h, uint8_t type, int len) {
    h[0]=FRAME_VER; h[1]=type; h[2]=(uint8_t)(len&0xFF); h[3]=(uint8_t)((len>>8)&0xFF);
}

TEST_CASE("reasm single chunk whole frame", "[reasm]") {
    frame_reasm_t r; frame_reasm_reset(&r);
    uint8_t f[4+3]; mk_hdr(f,0x01,3); f[4]='a'; f[5]='b'; f[6]='c';
    uint8_t type; const uint8_t *p; int len;
    TEST_ASSERT_EQUAL_INT(FRAME_READY, frame_reasm_push(&r,f,sizeof f,&type,&p,&len));
    TEST_ASSERT_EQUAL_UINT8(0x01,type);
    TEST_ASSERT_EQUAL_INT(3,len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(p,"abc",3));
}

TEST_CASE("reasm split across chunks (header split + payload split)", "[reasm]") {
    frame_reasm_t r; frame_reasm_reset(&r);
    uint8_t f[4+5]; mk_hdr(f,0x01,5); memcpy(f+4,"hello",5);
    uint8_t type; const uint8_t *p; int len;
    TEST_ASSERT_EQUAL_INT(FRAME_NEED_MORE, frame_reasm_push(&r,f,2,&type,&p,&len));
    TEST_ASSERT_EQUAL_INT(FRAME_NEED_MORE, frame_reasm_push(&r,f+2,3,&type,&p,&len));
    TEST_ASSERT_EQUAL_INT(FRAME_READY,     frame_reasm_push(&r,f+5,4,&type,&p,&len));
    TEST_ASSERT_EQUAL_INT(5,len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(p,"hello",5));
}

TEST_CASE("reasm rejects bad version and oversize len", "[reasm]") {
    frame_reasm_t r; uint8_t type; const uint8_t *p; int len;
    uint8_t bad[4]; mk_hdr(bad,0x01,3); bad[0]=9;
    frame_reasm_reset(&r);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_VER, frame_reasm_push(&r,bad,4,&type,&p,&len));
    uint8_t big[4]; mk_hdr(big,0x01, FRAME_MAX_PAYLOAD+1);
    frame_reasm_reset(&r);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_LEN, frame_reasm_push(&r,big,4,&type,&p,&len));
}

TEST_CASE("reasm zero-length payload frame", "[reasm]") {
    frame_reasm_t r; frame_reasm_reset(&r);
    uint8_t f[4]; mk_hdr(f,0x03,0);
    uint8_t type; const uint8_t *p; int len;
    TEST_ASSERT_EQUAL_INT(FRAME_READY, frame_reasm_push(&r,f,4,&type,&p,&len));
    TEST_ASSERT_EQUAL_UINT8(0x03,type);
    TEST_ASSERT_EQUAL_INT(0,len);
}
