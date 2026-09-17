// 公历 -> 农历 转换
// ---------------------------------------------------------------------------
// 数据表 LUNAR_INFO 见 lunar_data.h(由 _lunar_table.py 生成), 覆盖 1900..2100。
// 表起点: 公历 1900-01-31 = 农历 1900 年正月初一。
//
// 编码(每年一个 uint32_t):
//   bit 0..3   闰月月份(0 = 该年无闰月)
//   bit 15..4  十二月大小, bit15=正月 … bit4=十二月 (1 = 30 天, 0 = 29 天)
//   bit 16     闰月大小 (1 = 30 天, 0 = 29 天)
//
// 本文件只做"日期数值"的换算; 干支、中文月日名等在 main.cpp 里拼。
#pragma once
#include <stdint.h>

#include "lunar_data.h"

struct LunarDate {
  int  year;    // 农历年 (表中年份)
  int  month;   // 1..12
  int  day;     // 1..30
  bool leap;    // true = 闰月
  bool ok;      // false = 超出表覆盖范围(早于 1900-01-31 或晚于表尾)
};

static inline bool solarIsLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static inline int solarMonthDays(int y, int m) {
  static const int D[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && solarIsLeapYear(y)) return 29;
  return (m >= 1 && m <= 12) ? D[m] : 0;
}

// 公历日期距 1900-01-01 的天数(不含闰秒, 纯日历)
static inline long solarDayNum(int y, int m, int d) {
  long n = 0;
  for (int yy = 1900; yy < y; yy++) n += solarIsLeapYear(yy) ? 366 : 365;
  for (int mm = 1; mm < m; mm++) n += solarMonthDays(y, mm);
  return n + d - 1;
}

static inline int lunarLeapMonth(uint32_t info) { return (int)(info & 0xF); }
static inline int lunarMonthDays(uint32_t info, int month) {
  return ((info >> (16 - month)) & 1) ? 30 : 29;
}
static inline int lunarLeapMonthDays(uint32_t info) {
  return ((info >> 16) & 1) ? 30 : 29;
}

// 该农历年的总天数
static inline int lunarYearDays(uint32_t info) {
  int n = 0;
  for (int m = 1; m <= 12; m++) n += lunarMonthDays(info, m);
  if (lunarLeapMonth(info)) n += lunarLeapMonthDays(info);
  return n;
}

// 公历 -> 农历。超出 1900-01-31 .. 表尾 时返回 ok = false
static inline LunarDate solarToLunar(int y, int m, int d) {
  LunarDate r = {};
  long off = solarDayNum(y, m, d) - solarDayNum(1900, 1, 31);
  if (off < 0) return r;                       // 早于表起点

  int idx = 0;
  while (idx < LUNAR_COUNT) {
    int yd = lunarYearDays(LUNAR_INFO[idx]);
    if (off < yd) break;
    off -= yd;
    idx++;
  }
  if (idx >= LUNAR_COUNT) return r;            // 晚于表尾

  uint32_t info = LUNAR_INFO[idx];
  int leap = lunarLeapMonth(info);
  int mo = 12;
  bool lp = false;
  for (int mm = 1; mm <= 12; mm++) {
    int dm = lunarMonthDays(info, mm);
    if (off < dm) { mo = mm; lp = false; break; }
    off -= dm;
    if (leap == mm) {                          // 闰月紧跟被闰的那个月
      int dl = lunarLeapMonthDays(info);
      if (off < dl) { mo = mm; lp = true; break; }
      off -= dl;
    }
  }
  r.year  = LUNAR_YEAR0 + idx;
  r.month = mo;
  r.day   = (int)off + 1;
  r.leap  = lp;
  r.ok    = true;
  return r;
}
