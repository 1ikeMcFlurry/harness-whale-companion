// components/core/services/src/battery.c —— 电量纯逻辑(可 host 单测)
// 两种数据源共用同一套"平滑 + 迟滞档位":
//   - CW2017 电量计:battery_feed_pct(SOC%)  —— 直接吃百分比,轻 EMA
//   - ADC 电压方案:  battery_feed(mV)        —— EMA 电压后过 OCV 曲线
// 规范输出统一落到 f->pct / f->level,由 battery_percent()/battery_level() 读取。
#include "services/battery.h"

// 档位边界(百分比):>=80→5, 60~80→4, 40~60→3, 20~40→2, <20→1
static const int THR[BATTERY_LEVELS - 1] = {20, 40, 60, 80};
#define LVL_HYST 3        // 迟滞:跨界需超出边界 ±3,避免边界处档位抖动

// 虚拟锂电 OCV 曲线(mV→%),单调递减表,分段线性插值(仅 ADC 方案用)。
static const int OCV[][2] = {
    {4200, 100}, {4060, 90}, {3980, 80}, {3920, 70}, {3870, 60}, {3820, 50},
    {3790, 40},  {3770, 30}, {3740, 20}, {3680, 10}, {3450, 5},  {3000, 0},
};
#define OCV_N ((int)(sizeof OCV / sizeof OCV[0]))

int battery_curve_pct(int mv) {
    if (mv >= OCV[0][0]) return 100;
    if (mv <= OCV[OCV_N - 1][0]) return 0;
    for (int i = 0; i < OCV_N - 1; i++) {
        int hi_mv = OCV[i][0], lo_mv = OCV[i + 1][0];
        if (mv <= hi_mv && mv >= lo_mv) {
            int hi_p = OCV[i][1], lo_p = OCV[i + 1][1];
            return lo_p + (mv - lo_mv) * (hi_p - lo_p) / (hi_mv - lo_mv);
        }
    }
    return 0;
}

void battery_init(battery_filter_t *f) {
    f->ema = 0; f->primed = false; f->pct = 0; f->level = 0;
}

static int level_from_pct_plain(int pct) {   // 无迟滞:首次定档
    int l = 1;
    for (int i = 0; i < BATTERY_LEVELS - 1; i++) if (pct >= THR[i]) l++;
    return l;
}
// 整数 EMA 单步:cur 向 target 靠 (target-cur)/div;差不为 0 时至少走 1,
// 避免整数截断造成"死区"(如 (20-21)/4=0 会永远卡在 21,电量永久偏差几个点)。
static int ema_step(int cur, int target, int div) {
    int d = target - cur;
    int s = d / div;
    if (s == 0 && d != 0) s = (d > 0) ? 1 : -1;
    return cur + s;
}

static void apply_level_hyst(battery_filter_t *f, int pct) {
    int lvl = f->level ? f->level : 1;
    while (lvl < BATTERY_LEVELS && pct >= THR[lvl - 1] + LVL_HYST) lvl++;
    while (lvl > 1            && pct <  THR[lvl - 2] - LVL_HYST) lvl--;
    f->level = lvl;
}

int battery_feed_pct(battery_filter_t *f, int raw_pct) {
    if (raw_pct < 0) return f->pct;                     // 读失败:维持旧值
    if (raw_pct > 100) raw_pct = 100;
    if (!f->primed) {                                   // 首次:直接取真值,立即定档
        f->ema = raw_pct; f->primed = true;
        f->pct = raw_pct; f->level = level_from_pct_plain(raw_pct);
        return f->pct;
    }
    f->ema = ema_step(f->ema, raw_pct, 4);              // 轻 EMA(电量计本就平滑),精确收敛
    f->pct = f->ema;
    apply_level_hyst(f, f->pct);
    return f->pct;
}

int battery_feed(battery_filter_t *f, int raw_mv) {
    if (raw_mv < 0) return f->pct;                      // 读失败:维持旧值
    if (raw_mv > 4200 + 200) raw_mv = 4200 + 200;       // 兜底钳位
    if (raw_mv < 3000 - 300) raw_mv = 3000 - 300;
    if (!f->primed) {
        f->ema = raw_mv; f->primed = true;
        f->pct = battery_curve_pct(f->ema);
        f->level = level_from_pct_plain(f->pct);
        return f->pct;
    }
    f->ema = ema_step(f->ema, raw_mv, 8);               // EMA 平滑电压,防突变,精确收敛
    f->pct = battery_curve_pct(f->ema);
    apply_level_hyst(f, f->pct);
    return f->pct;
}

int battery_percent(const battery_filter_t *f) { return f->pct; }
int battery_level(const battery_filter_t *f)   { return f->level ? f->level : 1; }
