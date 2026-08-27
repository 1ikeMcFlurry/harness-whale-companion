// components/core/services/include/services/profile_ctl.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char     name[48];       // 47 字节 ≈ 15 个汉字(UTF-8 一个汉字 3 字节)
    char     role[40];       // 39 字节 ≈ 13 个汉字
    char     subtitle[56];   // 55 字节 ≈ 18 个汉字
    int      battery;        // 0..100
    int      level;          // 0..999
    int      xp;             // >=0
    int      xp_max;         // >=1
    bool     online;
    int      token;          // 积分余额 >=0
    int      token_max;      // token 进度条上限(默认 40000,可由 BLE 下发)
    int      sleep_min;      // 空闲后进入深度睡眠的分钟数;0=永不。默认 10
    int      game_total;     // 小游戏累计总积分(每局累加;同步后由小程序清零)
    int      game_best;      // 小游戏单局最高积分(同步后由小程序清零)
    uint8_t  img_mode;       // BLE 图片显示模式:0=头像位(默认) 1=全屏
    int      volume;         // codec 输出音量 0..100(默认 VOLUME_DEFAULT=95,可由 BLE 下发)
    bool     pet_enabled;    // 宠物功能开关(token 广播 op=0x05 控制);开则 dock 显示 PET
    uint8_t  pet_type;       // 宠物类型(1..N),pet_enabled 时有效
    char     avatar_name[16];// 内置头像资源名:[a-z0-9_-]{1,15}
} profile_data_t;

// 图片显示模式
#define IMG_MODE_AVATAR      0   // 显示到头像位置(96×156,裁剪填满,常驻)
#define IMG_MODE_FULLSCREEN  1   // 全屏显示约 6 秒后退回

#define TOKEN_MAX_DEFAULT     40000  // token 进度条默认上限(BLE 未下发 token_max 时用)
#define SLEEP_MIN_DEFAULT    10      // 深度睡眠默认空闲分钟数
#define SLEEP_MIN_MAX         1440   // 上限 24 小时
#define VOLUME_DEFAULT        95     // codec 输出音量默认 95%
#define PROFILE_BLOB_VER      14  // v14:追加内置头像资源名
#define PROFILE_BLOB_SIZE     (1 + (int)sizeof(profile_data_t))  // [ver][struct]

void profile_ctl_init(profile_data_t *d);   // 出厂默认

// 序列化到定长 blob(带版本头)。成功返回写入字节数,cap 不足返回 -1。
int  profile_serialize(const profile_data_t *d, uint8_t *buf, int cap);
// 从 blob 还原。版本/长度不符返回 -1,成功返回 0。
int  profile_deserialize(profile_data_t *d, const uint8_t *buf, int len);
