// components/core/services/include/services/tone_presets.h —— 内置提示音(RTTTL)
#pragma once

// 编进固件的短提示音,零存储成本、开机即可用、不会被误配置成静音。全部 < 1 秒。
// 加分:超级马里奥顶金币音。原曲是 B5 一个极短的倚音,紧接 E6 拉长。
//   d=32 → 41ms 的 b5(987.9Hz),4e6 → 333ms 的 e6(1319.9Hz),合计约 374ms。
//   两个音在 8kHz Q16 相位累加下的音准误差均 < 0.2 音分,与原曲一致。
#define TONE_TOKEN_ADD   "coin:d=32,o=6,b=180:b5,4e6"
// 扣分:仿马里奥"下水管/坠落"。C 大调琶音急速下行,195ms 一闪而过。
//   刻意做成最短最快的一个 —— 与金币音的"上行拖长"形成最强对比。
#define TONE_TOKEN_SUB   "down:d=32,o=5,b=190:e6,c6,g,e,c"
// 失败:仿马里奥死亡音开头 B4-F5-(停)-F5-E5-C5。先升后降 + 中间留白,
//   664ms 是三者里最慢的,音域也最低 —— 速度/方向/音域三个维度都与另两个错开,
//   小喇叭上也不会糊成一片。
#define TONE_TOKEN_FAIL  "die:d=16,o=4,b=180:b,f5,16p,f5,8e5,8c5"

// 马里奥跳跃(收到心跳广播):跳一下顶一枚金币,与加分共用同一枚音 ——
//   屏幕上本来就是同一个动作,声音再分两种反而对不上。
//   ⚠ 心跳广播每 ~100ms 就来一条(扫描不去重),播放侧必须限速,否则是连发噪音。
#define TONE_MARIO_COIN  TONE_TOKEN_ADD

// 产测不良告警(循环播放):两声急促高音,醒目;循环间隔由调用方控制。
#define TONE_FAIL_ALARM  "warn:d=8,o=7,b=210:c,16p,c"
// 产测良品提示:上行短促三音。
#define TONE_PASS_OK     "ok:d=16,o=6,b=200:c,e,g"
