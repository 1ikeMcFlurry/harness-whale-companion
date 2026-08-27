#include <string.h>

#include "unity.h"
#include "services/profile_bcast.h"

static const uint8_t SELF[6] = {1, 2, 3, 4, 5, 6};

static int fake_mac(const uint8_t *m, int n, uint8_t out[8], void *user) {
    (void)user;
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < n; ++i) { h ^= m[i]; h *= 1099511628211ULL; }
    for (int i = 0; i < 8; ++i) out[i] = (uint8_t)(h >> (i * 8));
    return 0;
}

static void frame(uint8_t out[26], uint8_t field, uint16_t txn, uint8_t index,
                  uint8_t count, uint8_t total, const uint8_t data[3]) {
    memset(out, 0, 26);
    out[0]='H'; out[1]='B'; out[2]=0x23; out[3]=field;
    memcpy(out+4, SELF, 6); out[10]=(uint8_t)txn; out[11]=(uint8_t)(txn>>8);
    out[12]=index; out[13]=count; out[14]=total; memcpy(out+15,data,3);
    fake_mac(out, 18, out+18, NULL);
}

TEST_CASE("profile broadcast completes uint32 from out of order fragments", "[profile_bcast]") {
    profile_bcast_t state; pb_result_t result; uint8_t f[26];
    profile_bcast_init(&state, SELF, fake_mac, NULL);
    const uint8_t tail[3]={0x12,0,0}; frame(f,PB_FIELD_TOKEN,7,1,2,4,tail);
    TEST_ASSERT_EQUAL_INT(PB_MORE, profile_bcast_feed(&state,f,100,&result));
    const uint8_t head[3]={0x78,0x56,0x34}; frame(f,PB_FIELD_TOKEN,7,0,2,4,head);
    TEST_ASSERT_EQUAL_INT(PB_COMPLETE, profile_bcast_feed(&state,f,101,&result));
    TEST_ASSERT_EQUAL_INT(PB_FIELD_TOKEN,result.field); TEST_ASSERT_EQUAL_UINT8(4,result.len);
    const uint8_t expected[4]={0x78,0x56,0x34,0x12};
    TEST_ASSERT_EQUAL_MEMORY(expected,result.data,4);
}

TEST_CASE("profile broadcast accepts exact duplicate and rejects conflicting duplicate", "[profile_bcast]") {
    profile_bcast_t state; pb_result_t result; uint8_t f[26]; const uint8_t a[3]={1,2,3};
    profile_bcast_init(&state,SELF,fake_mac,NULL); frame(f,PB_FIELD_NICKNAME,8,0,2,4,a);
    TEST_ASSERT_EQUAL_INT(PB_MORE,profile_bcast_feed(&state,f,1,&result));
    TEST_ASSERT_EQUAL_INT(PB_MORE,profile_bcast_feed(&state,f,2,&result));
    const uint8_t bad[3]={9,2,3}; frame(f,PB_FIELD_NICKNAME,8,0,2,4,bad);
    TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,3,&result));
}

TEST_CASE("profile broadcast validates target mac signature shape padding and field", "[profile_bcast]") {
    profile_bcast_t state; pb_result_t result; uint8_t f[26]; const uint8_t d[3]={1,0,0};
    profile_bcast_init(&state,SELF,fake_mac,NULL); frame(f,PB_FIELD_AVATAR_NAME,9,0,1,1,d);
    f[4]^=1; TEST_ASSERT_EQUAL_INT(PB_IGNORE,profile_bcast_feed(&state,f,0,&result));
    frame(f,PB_FIELD_AVATAR_NAME,9,0,1,1,d); f[18]^=1;
    TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,0,&result));
    frame(f,99,9,0,1,1,d); TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,0,&result));
    frame(f,PB_FIELD_AVATAR_NAME,9,1,1,1,d); TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,0,&result));
    const uint8_t pad[3]={'a',1,0}; frame(f,PB_FIELD_AVATAR_NAME,9,0,1,1,pad);
    TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,0,&result));
}

TEST_CASE("profile broadcast times out across uint32 wrap and replaces transaction", "[profile_bcast]") {
    profile_bcast_t state; pb_result_t result; uint8_t f[26]; const uint8_t a[3]={'a','b','c'};
    profile_bcast_init(&state,SELF,fake_mac,NULL); frame(f,PB_FIELD_NICKNAME,10,0,2,4,a);
    TEST_ASSERT_EQUAL_INT(PB_MORE,profile_bcast_feed(&state,f,0xfffff800u,&result));
    const uint8_t tail[3]={'d',0,0}; frame(f,PB_FIELD_NICKNAME,10,1,2,4,tail);
    TEST_ASSERT_EQUAL_INT(PB_MORE,profile_bcast_feed(&state,f,0x00000400u,&result));
    const uint8_t x[3]={'x',0,0}; frame(f,PB_FIELD_AVATAR_NAME,11,0,1,1,x);
    TEST_ASSERT_EQUAL_INT(PB_COMPLETE,profile_bcast_feed(&state,f,0x401,&result));
    TEST_ASSERT_EQUAL_INT(PB_FIELD_AVATAR_NAME,result.field);
}

TEST_CASE("profile broadcast suppresses completed replay and validates complete values", "[profile_bcast]") {
    profile_bcast_t state; pb_result_t result; uint8_t f[26]; const uint8_t x[3]={'x',0,0};
    profile_bcast_init(&state,SELF,fake_mac,NULL); frame(f,PB_FIELD_AVATAR_NAME,12,0,1,1,x);
    TEST_ASSERT_EQUAL_INT(PB_COMPLETE,profile_bcast_feed(&state,f,1,&result));
    TEST_ASSERT_EQUAL_INT(PB_IGNORE,profile_bcast_feed(&state,f,2,&result));
    const uint8_t invalid[3]={'B','a','d'}; frame(f,PB_FIELD_AVATAR_NAME,13,0,1,3,invalid);
    TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,3,&result));
    const uint8_t zero[3]={0,0,0}; frame(f,PB_FIELD_TOKEN_MAX,14,0,2,4,zero);
    TEST_ASSERT_EQUAL_INT(PB_MORE,profile_bcast_feed(&state,f,4,&result));
    frame(f,PB_FIELD_TOKEN_MAX,14,1,2,4,zero);
    TEST_ASSERT_EQUAL_INT(PB_ERROR,profile_bcast_feed(&state,f,5,&result));
}

TEST_CASE("profile broadcast assembles maximum 47 byte nickname across 16 fragments", "[profile_bcast]") {
    profile_bcast_t state; pb_result_t result; uint8_t f[26]; uint8_t value[47];
    memset(value, 'a', sizeof value); profile_bcast_init(&state,SELF,fake_mac,NULL);
    for (int index = 15; index >= 0; --index) {
        uint8_t data[3] = {0}; int offset = index * 3; int length = 47 - offset;
        if (length > 3) length = 3; memcpy(data, value + offset, (size_t)length);
        frame(f,PB_FIELD_NICKNAME,15,(uint8_t)index,16,47,data);
        pb_status_t expected = index == 0 ? PB_COMPLETE : PB_MORE;
        TEST_ASSERT_EQUAL_INT(expected,
            profile_bcast_feed(&state,f,(uint32_t)(115-index),&result));
    }
    TEST_ASSERT_EQUAL_UINT8(47,result.len);
    TEST_ASSERT_EQUAL_MEMORY(value,result.data,47);
}
