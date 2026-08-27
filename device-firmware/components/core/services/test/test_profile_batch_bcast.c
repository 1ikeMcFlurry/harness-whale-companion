#include <string.h>

#include "unity.h"
#include "services/profile_batch_bcast.h"

static int fake_mac(const uint8_t *m, int n, uint8_t out[8], void *user) {
    (void)user;
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < n; ++i) { h ^= m[i]; h *= 1099511628211ULL; }
    for (int i = 0; i < 8; ++i) out[i] = (uint8_t)(h >> (i * 8));
    return 0;
}

static void sign_frame(uint8_t f[26]) { fake_mac(f, 18, f + 18, NULL); }

static void common(uint8_t f[26], uint16_t txn, uint8_t fields,
                   uint32_t token, uint32_t token_max, uint32_t epoch, uint8_t avatar) {
    memset(f, 0, 26); f[0]='H'; f[1]='B'; f[2]=PBB_HDR_COMMON;
    f[3]=(uint8_t)txn; f[4]=(uint8_t)(txn>>8); f[5]=fields;
    f[6]=token; f[7]=token>>8; f[8]=token>>16;
    f[9]=token_max; f[10]=token_max>>8; f[11]=token_max>>16;
    f[12]=epoch; f[13]=epoch>>8; f[14]=epoch>>16; f[15]=epoch>>24;
    f[16]=avatar; sign_frame(f);
}

static void nick(uint8_t f[26], uint16_t txn, uint8_t index, uint8_t count,
                 uint8_t total, const uint8_t *data, uint8_t used) {
    memset(f, 0, 26); f[0]='H'; f[1]='B'; f[2]=PBB_HDR_NICKNAME;
    f[3]=(uint8_t)txn; f[4]=(uint8_t)(txn>>8);
    f[5]=(uint8_t)(((count-1u)<<4)|index); f[6]=total;
    memcpy(f+7,data,used); sign_frame(f);
}

TEST_CASE("batch common packet returns all selected fields atomically", "[profile_batch]") {
    profile_batch_bcast_t s; pbb_result_t r; uint8_t f[26];
    profile_batch_bcast_init(&s,fake_mac,NULL);
    common(f,0x1234,PBB_FIELD_ALL,40000,80000,0x65010203,7);
    TEST_ASSERT_EQUAL_INT(PBB_COMMON_READY,profile_batch_bcast_feed(&s,f,1,&r));
    TEST_ASSERT_EQUAL_HEX8(PBB_FIELD_ALL,r.fields);
    TEST_ASSERT_EQUAL_UINT32(40000,r.token); TEST_ASSERT_EQUAL_UINT32(80000,r.token_max);
    TEST_ASSERT_EQUAL_UINT32(0x65010203,r.time_epoch); TEST_ASSERT_EQUAL_UINT8(7,r.avatar_id);
    TEST_ASSERT_EQUAL_INT(PBB_COMMON_READY,profile_batch_bcast_feed(&s,f,2,&r));
    profile_batch_bcast_commit(&s,PBB_HDR_COMMON,0x1234);
    TEST_ASSERT_EQUAL_INT(PBB_IGNORE,profile_batch_bcast_feed(&s,f,2,&r));
}

TEST_CASE("batch common validates bitmap absent values and signature", "[profile_batch]") {
    profile_batch_bcast_t s; pbb_result_t r; uint8_t f[26];
    profile_batch_bcast_init(&s,fake_mac,NULL);
    common(f,1,PBB_FIELD_TOKEN,12,0,0,0); f[18]^=1;
    TEST_ASSERT_EQUAL_INT(PBB_IGNORE,profile_batch_bcast_feed(&s,f,1,&r));
    common(f,2,PBB_FIELD_TOKEN,12,9,0,0);
    TEST_ASSERT_EQUAL_INT(PBB_ERROR,profile_batch_bcast_feed(&s,f,2,&r));
    common(f,3,PBB_FIELD_TOKEN_MAX,0,0,0,0);
    TEST_ASSERT_EQUAL_INT(PBB_ERROR,profile_batch_bcast_feed(&s,f,3,&r));
}

TEST_CASE("nickname one packet and five packets complete", "[profile_batch]") {
    profile_batch_bcast_t s; pbb_result_t r; uint8_t f[26];
    profile_batch_bcast_init(&s,fake_mac,NULL);
    const uint8_t short_name[]="xiaozhou";
    nick(f,10,0,1,8,short_name,8);
    TEST_ASSERT_EQUAL_INT(PBB_NICKNAME_READY,profile_batch_bcast_feed(&s,f,1,&r));
    TEST_ASSERT_EQUAL_UINT8(8,r.nickname_len); TEST_ASSERT_EQUAL_MEMORY(short_name,r.nickname,8);

    uint8_t long_name[47]; memset(long_name,'a',sizeof long_name);
    for (int i=4;i>=0;--i) {
        uint8_t used=(uint8_t)(47-i*11); if(used>11) used=11;
        nick(f,11,(uint8_t)i,5,47,long_name+i*11,used);
        TEST_ASSERT_EQUAL_INT(i==0?PBB_NICKNAME_READY:PBB_MORE,
                              profile_batch_bcast_feed(&s,f,(uint32_t)(10-i),&r));
    }
    TEST_ASSERT_EQUAL_UINT8(47,r.nickname_len); TEST_ASSERT_EQUAL_MEMORY(long_name,r.nickname,47);
}

TEST_CASE("nickname rejects conflicting duplicate padding and invalid utf8", "[profile_batch]") {
    profile_batch_bcast_t s; pbb_result_t r; uint8_t f[26];
    profile_batch_bcast_init(&s,fake_mac,NULL);
    const uint8_t a[11]="abcdefghij"; nick(f,20,0,2,12,a,11);
    TEST_ASSERT_EQUAL_INT(PBB_MORE,profile_batch_bcast_feed(&s,f,1,&r));
    f[7]^=1; sign_frame(f);
    TEST_ASSERT_EQUAL_INT(PBB_ERROR,profile_batch_bcast_feed(&s,f,2,&r));
    const uint8_t bad[]={0xc0}; nick(f,21,0,1,1,bad,1);
    TEST_ASSERT_EQUAL_INT(PBB_ERROR,profile_batch_bcast_feed(&s,f,3,&r));
    const uint8_t x[]={'x'}; nick(f,22,0,1,1,x,1); f[8]=1; sign_frame(f);
    TEST_ASSERT_EQUAL_INT(PBB_ERROR,profile_batch_bcast_feed(&s,f,4,&r));
}
