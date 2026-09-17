# -*- coding: utf-8 -*-
"""时间起卦(梅花易数·年月日时起例)的独立校验。

不复用 C++ 代码 —— 按《梅花易数》原文重新实现一遍公式, 再拿原书筮例对答案。

基准筮例(均出自《梅花易数》卷一, 日期是**农历**, 原书如此):
  ① 观梅占: 辰年十二月十七日申时 -> 泽火革(49) 初爻动 -> 泽山咸(31)
  ② 牡丹占: 巳年三月十六日卯时   -> 天风姤(44) 五爻动 -> 火风鼎(50)

另校验:
  公历->农历换算(对已知基准点) / 年支 / 时辰 / 闰年天数 /
  公历->农历->卦 完整链路 / 64 卦与 6 爻位的可达性 / 输入串边界 / 分布

分工说明:
  · 农历**表**的正确性   -> _lunar_table.py   (用 ephem 实算朔时刻逐个初一裁决)
  · 公历->农历**算法**    -> _scratch/verify_conv.py (与 lunardate / lunar_python 逐日比对)
  · 起卦**公式**          -> 本脚本
"""
import io
import re
import sys
import datetime
from collections import Counter

# ---------- 1. 从 gddy_data.h 里取 64 卦的卦名与卦序(与实际固件同一份数据) ----------
raw = io.open('src/gddy_data.h', encoding='utf-8').read()
pairs = []
for m in re.finditer(r'\{\s*(\d+)\s*,\s*"([^"]{2,6})"', raw):
    num, nm = int(m.group(1)), m.group(2)
    if 1 <= num <= 64 and nm not in ('全卦总论',):
        pairs.append((num, nm))
        if len(pairs) == 64:
            break
assert len(pairs) == 64, '没解析出 64 卦, 得到 %d' % len(pairs)
BY_NUM = dict(pairs)
assert list(BY_NUM.keys()) == list(range(1, 65)), '卦序不是 1..64'

# ---------- 2. 卦名 -> 六爻 bits (与 main.cpp 的 hexagramLines 同一套约定) ----------
TRI = {
    '乾': 7, '天': 7, '坤': 0, '地': 0,
    '震': 1, '雷': 1, '巽': 6, '风': 6,
    '坎': 2, '水': 2, '离': 5, '火': 5,
    '艮': 4, '山': 4, '兑': 3, '泽': 3,
}


def lines_of(name):
    """bit0..bit5 = 初爻..上爻 (1=阳)。卦名第 1 字=上卦, 第 2 字=下卦。"""
    if name[1] == '为':                       # 八纯卦: 乾为天 / 坤为地 …
        t = TRI[name[0]]
        return t | (t << 3)
    return TRI[name[1]] | (TRI[name[0]] << 3)


BITS2NUM = {lines_of(nm): num for num, nm in pairs}
assert len(BITS2NUM) == 64, '64 卦的六爻不构成双射! 只有 %d 种' % len(BITS2NUM)

# ---------- 3. 八卦数与三爻的关系 ----------
XT_BITS = {0: 0, 1: 7, 2: 3, 3: 5, 4: 1, 5: 6, 6: 2, 7: 4, 8: 0}   # 先天数 -> bits
GUA8 = {1: '乾', 2: '兑', 3: '离', 4: '震', 5: '巽', 6: '坎', 7: '艮', 8: '坤'}
YAO = ['', '初', '二', '三', '四', '五', '上']
ZHI = ['', '子', '丑', '寅', '卯', '辰', '巳', '午', '未', '申', '酉', '戌', '亥']


# ---------- 4. 农历表(读 C++ 用的同一份 src/lunar_data.h) ----------
lh = io.open('src/lunar_data.h', encoding='utf-8').read()
_i = lh.index('LUNAR_INFO[LUNAR_COUNT] = {')
_j = lh.index('};', _i)
LUNAR_INFO = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{5})', lh[_i:_j])]
LUNAR_YEAR0 = 1900
assert len(LUNAR_INFO) == 201, '农历表应为 201 项(1900..2100), 得到 %d' % len(LUNAR_INFO)


def solar_is_leap_year(y):
    return (y % 4 == 0 and y % 100 != 0) or (y % 400 == 0)


def solar_month_days(y, m):
    D = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    if m == 2 and solar_is_leap_year(y):
        return 29
    return D[m]


def solar_day_num(y, m, d):
    n = 0
    for yy in range(1900, y):
        n += 366 if solar_is_leap_year(yy) else 365
    for mm in range(1, m):
        n += solar_month_days(y, mm)
    return n + d - 1


def lunar_leap_month(info):
    return info & 0xF


def lunar_month_days(info, month):
    return 30 if (info >> (16 - month)) & 1 else 29


def lunar_leap_month_days(info):
    return 30 if (info >> 16) & 1 else 29


def lunar_year_days(info):
    n = sum(lunar_month_days(info, m) for m in range(1, 13))
    if lunar_leap_month(info):
        n += lunar_leap_month_days(info)
    return n


def solar_to_lunar(y, m, d):
    """-> (农历年, 月, 日, 是否闰月) 或 None(超出表范围)"""
    off = solar_day_num(y, m, d) - solar_day_num(1900, 1, 31)
    if off < 0:
        return None
    idx = 0
    while idx < len(LUNAR_INFO):
        yd = lunar_year_days(LUNAR_INFO[idx])
        if off < yd:
            break
        off -= yd
        idx += 1
    if idx >= len(LUNAR_INFO):
        return None
    info = LUNAR_INFO[idx]
    leap = lunar_leap_month(info)
    mo, lp = 12, False
    for mm in range(1, 13):
        dm = lunar_month_days(info, mm)
        if off < dm:
            mo, lp = mm, False
            break
        off -= dm
        if leap == mm:
            dl = lunar_leap_month_days(info)
            if off < dl:
                mo, lp = mm, True
                break
            off -= dl
    return (LUNAR_YEAR0 + idx, mo, off + 1, lp)


# ---------- 5. 公式(照《梅花易数》原文) ----------
def cast(upper_v, lower_v, dong_v):
    """upper_v/lower_v: 先天卦数 1..8;  dong_v: 1..6"""
    bits = XT_BITS[lower_v] | (XT_BITS[upper_v] << 3)
    return BITS2NUM[bits], BITS2NUM[bits ^ (1 << (dong_v - 1))], bits


def cast_time(year_dzhi, month, day, shichen):
    """年支数(子1…亥12) + 农历月 + 农历日 + 时辰数(子1…亥12) -> 本卦/动爻/之卦"""
    a = year_dzhi + month + day
    b = a + shichen
    r_upper = a % 8 or 8        # 上卦 = 年月日 ÷ 8
    r_lower = b % 8 or 8        # 下卦 = 年月日时 ÷ 8
    dong = b % 6 or 6           # 动爻 = 年月日时 ÷ 6
    ben, bian, bits = cast(r_upper, r_lower, dong)
    return dict(a=a, b=b, upper=r_upper, lower=r_lower, dong=dong,
                ben=ben, bian=bian, bits=bits)


def cast_solar(y, m, d, h):
    """完整链路: 公历 -> 农历 -> 卦。返回 None 表示超出农历表范围"""
    ld = solar_to_lunar(y, m, d)
    if ld is None:
        return None
    ly, lm, lday, leap = ld
    r = cast_time(zhi_of_year(ly), lm, lday, shichen_of_hour(h))
    r.update(ly=ly, lm=lm, ld=lday, leap=leap)
    return r


def zhi_of_year(lunar_year):
    """**农历年** -> 年支序数 子=1 (公元 4 年 = 甲子)"""
    return (lunar_year - 4) % 12 + 1


def shichen_of_hour(h):
    """0..23 时 -> 时辰序数 子=1 (子时跨 23:00~00:59)"""
    return ((h + 1) // 2) % 12 + 1


days_in_month = solar_month_days

# ---------- 6. 跑校验 ----------
fails = []


def chk(cond, label, extra=''):
    print('  %s %s%s' % ('OK  ' if cond else 'FAIL', label, ('  ' + extra) if extra else ''))
    if not cond:
        fails.append(label)


print('=== ① 观梅占(农历: 辰年十二月十七日申时) ===')
r = cast_time(5, 12, 17, 9)          # 辰=5, 申=9
print('  年月日=%d 年月日时=%d 上卦%d(%s) 下卦%d(%s) 动爻%d'
      % (r['a'], r['b'], r['upper'], GUA8[r['upper']], r['lower'], GUA8[r['lower']], r['dong']))
print('  本卦 = %s 第%d卦 ;  之卦 = %s 第%d卦'
      % (BY_NUM[r['ben']], r['ben'], BY_NUM[r['bian']], r['bian']))
chk(BY_NUM[r['ben']] == '泽火革', '观梅 · 本卦应为 泽火革', '实际 %s' % BY_NUM[r['ben']])
chk(r['dong'] == 1, '观梅 · 动爻应为 初爻', '实际 %s爻' % YAO[r['dong']])
chk(BY_NUM[r['bian']] == '泽山咸', '观梅 · 之卦应为 泽山咸', '实际 %s' % BY_NUM[r['bian']])

print('=== ② 牡丹占(农历: 巳年三月十六日卯时) ===')
r = cast_time(6, 3, 16, 4)           # 巳=6, 卯=4
print('  年月日=%d 年月日时=%d 上卦%d(%s) 下卦%d(%s) 动爻%d'
      % (r['a'], r['b'], r['upper'], GUA8[r['upper']], r['lower'], GUA8[r['lower']], r['dong']))
print('  本卦 = %s 第%d卦 ;  之卦 = %s 第%d卦'
      % (BY_NUM[r['ben']], r['ben'], BY_NUM[r['bian']], r['bian']))
chk(BY_NUM[r['ben']] == '天风姤', '牡丹 · 本卦应为 天风姤', '实际 %s' % BY_NUM[r['ben']])
chk(r['dong'] == 5, '牡丹 · 动爻应为 五爻', '实际 %s爻' % YAO[r['dong']])
chk(BY_NUM[r['bian']] == '火风鼎', '牡丹 · 之卦应为 火风鼎', '实际 %s' % BY_NUM[r['bian']])

print('=== ③ 公历 -> 农历(已知基准点, 含春节与闰月) ===')
BASE = [                       # (公历, 农历年, 月, 日, 是否闰月, 说明)
    ((1900, 1, 31), 1900, 1, 1, False, '表起点'),
    ((2020, 5, 23), 2020, 4, 1, True, '闰四月初一'),
    ((2023, 3, 22), 2023, 2, 1, True, '闰二月初一'),
    ((2024, 2, 10), 2024, 1, 1, False, '春节'),
    ((2025, 1, 29), 2025, 1, 1, False, '春节'),
    ((2025, 3, 17), 2025, 2, 18, False, ''),
    ((2026, 2, 17), 2026, 1, 1, False, '春节'),
    ((2026, 9, 16), 2026, 8, 6, False, ''),
    ((2026, 9, 17), 2026, 8, 7, False, ''),
    ((2026, 9, 18), 2026, 8, 8, False, ''),
    ((2033, 12, 22), 2033, 11, 1, True, '闰十一月初一'),
    ((2100, 12, 31), 2100, 12, 1, False, '表尾附近'),
]
for (y, m, d), ly, lm, ld, lp, note in BASE:
    got = solar_to_lunar(y, m, d)
    want = (ly, lm, ld, lp)
    chk(got == want, '%04d-%02d-%02d -> 农历%d年%s%d月%d日' % (y, m, d, ly, '闰' if lp else '', lm, ld),
        ('%s %s' % (note, '实际 %s' % (got,))) if got != want else note)
chk(solar_to_lunar(1900, 1, 30) is None, '1900-01-30 早于表起点 -> 判为不可用')

print('=== ④ 年支换算(对已知干支年, 传农历年) ===')
for y, z in ((1984, 1), (1924, 1), (2024, 5), (2025, 6), (2026, 7), (2027, 8), (1204, 1), (2020, 1)):
    got = zhi_of_year(y)
    chk(got == z, '农历 %d 年 -> %s(%d)' % (y, ZHI[z], z), '实际 %s(%d)' % (ZHI[got], got))

print('=== ⑤ 时辰换算(全 24 小时) ===')
expect = [1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 1]
tbl = [shichen_of_hour(h) for h in range(24)]
print('  ' + ' '.join('%02d=%s' % (h, ZHI[tbl[h]]) for h in range(24)))
chk(tbl == expect, '0~23 时全部分配正确')

print('=== ⑥ 完整链路: 公历 -> 农历 -> 卦 ===')
LINK = [   # (公历 ymd h, 期望农历, 期望本卦, 期望动爻, 期望之卦)
    ((2025, 3, 17, 14), '2025年2月18日', '兑为泽', 4, '水泽节'),
    ((2026, 2, 17, 0),  '2026年1月1日',  '天泽履', 4, '风泽中孚'),
    ((2026, 9, 17, 15), '2026年8月7日',  '水山蹇', 1, '水火既济'),
]
for (y, m, d, h), wl, wben, wdong, wbian in LINK:
    r = cast_solar(y, m, d, h)
    got_l = '%d年%s%d月%d日' % (r['ly'], '闰' if r['leap'] else '', r['lm'], r['ld'])
    chk(got_l == wl, '%04d-%02d-%02d %02d时 的农历' % (y, m, d, h), '实际 %s' % got_l)
    line = ('    年支%s(%d) + 月%d + 日%d = %d ; 加%s时(%d) = %d  ->  上卦%s 下卦%s %s爻动'
            % (ZHI[zhi_of_year(r['ly'])], zhi_of_year(r['ly']), r['lm'], r['ld'], r['a'],
               ZHI[shichen_of_hour(h)], shichen_of_hour(h), r['b'],
               GUA8[r['upper']], GUA8[r['lower']], YAO[r['dong']]))
    print(line)
    print('    本卦 %s(%d)  之卦 %s(%d)'
          % (BY_NUM[r['ben']], r['ben'], BY_NUM[r['bian']], r['bian']))
    if wben:
        chk(BY_NUM[r['ben']] == wben, '本卦应为 %s' % wben, '实际 %s' % BY_NUM[r['ben']])
        chk(r['dong'] == wdong, '动爻应为 %s爻' % YAO[wdong], '实际 %s爻' % YAO[r['dong']])
        if wbian:
            chk(BY_NUM[r['bian']] == wbian, '之卦应为 %s' % wbian, '实际 %s' % BY_NUM[r['bian']])

print('=== ⑦ 64 卦 / 6 爻位可达性(八卦数 8×8, 动爻 6) ===')
bens, bians = set(), set()
for u in range(1, 9):
    for l in range(1, 9):
        for d in range(1, 7):
            b, x, _ = cast(u, l, d)
            bens.add(b)
            bians.add(x)
chk(len(bens) == 64, '8×8 组合覆盖全部 64 卦', '实际 %d' % len(bens))
chk(len(bians) == 64, '之卦同样覆盖 64 卦', '实际 %d' % len(bians))

print('=== ⑧ 动爻自反性: 同一爻位再变一次应回到本卦 ===')
ok = True
for u in range(1, 9):
    for l in range(1, 9):
        for d in range(1, 7):
            ben, bian, bits = cast(u, l, d)
            if BITS2NUM[bits ^ (1 << (d - 1)) ^ (1 << (d - 1))] != ben:
                ok = False
chk(ok, '初爻..上爻逐个自反')

print('=== ⑨ 闰年天数 ===')
for y, m, d in ((2024, 2, 29), (2026, 2, 28), (2000, 2, 29), (1900, 2, 28),
                (2100, 2, 28), (2026, 4, 30), (2026, 9, 30), (2026, 1, 31), (2026, 11, 30)):
    chk(days_in_month(y, m) == d, '%d 年 %d 月 -> %d 天' % (y, m, d), '实际 %d' % days_in_month(y, m))

print('=== ⑩ 公历 10 位串解析 + 合法性 ===')


def parse(s):
    return int(s[0:4]), int(s[4:6]), int(s[6:8]), int(s[8:10])


def valid(y, m, d, h):
    if not (1900 <= y <= 2100):        return '年份需 1900-2100'
    if not (1 <= m <= 12):             return '月份需 01-12'
    if d < 1:                          return '日期需 01 起'
    if d > days_in_month(y, m):        return '该月没有这一天'
    if h > 23:                         return '小时需 00-23'
    if solar_to_lunar(y, m, d) is None: return '农历表自 1900-01-31 起'
    return None


cases = [('2026091714', None), ('2026022812', None), ('2024022912', None),
         ('2023022900', '该月没有这一天'), ('2026130112', '月份需 01-12'),
         ('2026090023', '日期需 01 起'), ('2026091724', '小时需 00-23'),
         ('1800010112', '年份需 1900-2100'), ('1900010112', '农历表自 1900-01-31 起'),
         ('1900013112', None)]
for s, want in cases:
    y, m, d, h = parse(s)
    got = valid(y, m, d, h)
    chk(got == want, '输入 %s (%d-%02d-%02d %02d时)' % (s, y, m, d, h), '校验=%s' % (got or '通过'))

print('=== ⑪ 分布粗查(公历 2026 全年, 每天 12 个时辰) ===')
cb, cd = Counter(), Counter()
for day in range(1, 366):
    dt = datetime.date(2026, 1, 1) + datetime.timedelta(days=day - 1)
    for h in (0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22):
        r = cast_solar(dt.year, dt.month, dt.day, h)
        cb[r['ben']] += 1
        cd[r['dong']] += 1
print('  出现过的本卦种数: %d / 64 ; 每个本卦出现次数 min=%d max=%d'
      % (len(cb), min(cb.values()), max(cb.values())))
print('  动爻分布(初..上): %s' % [cd[i] for i in range(1, 7)])
chk(len(cb) >= 60, '全年 4380 次起卦能覆盖大部分卦')

print()
if fails:
    print('!! 有 %d 项未通过: %s' % (len(fails), fails))
    sys.exit(1)
print('全部通过: 农历换算正确, 时间起卦公式与原书筮例一致, 无越界。')
