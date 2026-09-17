# -*- coding: utf-8 -*-
"""校验数字卦换算逻辑: 1) 64 卦六爻解析是否为 0..63 的双射
2) 傅佩荣换算是否与公开示例一致  3) 随机分布的均匀性"""
import re
from collections import Counter

src = open('D:/cardputer/gddy/src/gddy_data.h', encoding='utf-8').read()
recs = re.findall(r'\{(\d+),\s*"([^"]*)"', src)
nums = [int(n) for n, _ in recs]
names = [nm for _, nm in recs]
assert len(names) == 64, 'parsed %d names' % len(names)

SYM = {'乾': 7, '天': 7, '坤': 0, '地': 0, '震': 1, '雷': 1, '巽': 6, '风': 6,
       '坎': 2, '水': 2, '离': 5, '火': 5, '艮': 4, '山': 4, '兑': 3, '泽': 3}


def trigram(ch):
    if ch not in SYM:
        return None
    return SYM[ch]


def lines(name):
    if len(name) < 2:
        return None
    if name[1] == '为':                      # 八纯卦: 上下同
        t = trigram(name[0])
        return None if t is None else (t | (t << 3))
    up = trigram(name[0])                    # 第1字 = 上卦(外)
    lo = trigram(name[1])                    # 第2字 = 下卦(内)
    if up is None or lo is None:
        return None
    return lo | (up << 3)


bits_list = [lines(n) for n in names]
bad = [(nums[i], names[i]) for i in range(64) if bits_list[i] is None]
print('解析失败:', bad if bad else '无')

if not bad:
    dup = [b for b, c in Counter(bits_list).items() if c > 1]
    miss = sorted(set(range(64)) - set(bits_list))
    print('重复 bits:', dup if dup else '无')
    print('缺失 bits:', miss if miss else '无')
    print('是否 0..63 双射:', sorted(bits_list) == list(range(64)))

XT = {1: 7, 2: 3, 3: 5, 4: 1, 5: 6, 6: 2, 7: 4, 8: 0}
YAO = ['', '初', '二', '三', '四', '五', '上']


def cast(n1, n2, n3):
    r1 = n1 % 8 or 8
    r2 = n2 % 8 or 8
    r3 = n3 % 6 or 6
    bits = XT[r1] | (XT[r2] << 3)
    idx = bits_list.index(bits)
    bian = bits ^ (1 << (r3 - 1))
    bidx = bits_list.index(bian)
    return dict(r=(r1, r2, r3), ben=names[idx], bennum=nums[idx],
                dong=YAO[r3] + '爻', zhi=names[bidx], zhinum=nums[bidx])


print()
print('=== 公开示例核对 ===')
for trio in [(431, 379, 847), (493, 809, 793), (130, 174, 314)]:
    r = cast(*trio)
    print('%s -> 余%s  本卦 %s(%d)  %s  之卦 %s(%d)'
          % (trio, r['r'], r['ben'], r['bennum'], r['dong'], r['zhi'], r['zhinum']))

print()
print('=== 随机分布 (100 万次) ===')
import random
random.seed(7)
cb, cz, cd = Counter(), Counter(), Counter()
for _ in range(1000000):
    r = cast(random.randint(100, 999), random.randint(100, 999), random.randint(100, 999))
    cb[r['r'][0]] += 1
    cz[r['ben']] += 1
    cd[r['dong']] += 1
print('下卦余数分布:', {k: round(v / 1000000 * 100, 2) for k, v in sorted(cb.items())})
print('动爻分布  :', {k: round(v / 1000000 * 100, 2) for k, v in sorted(cd.items())})
print('本卦最多/最少: %s %d次  /  %s %d次'
      % (cb and cz.most_common(1)[0][0], cz.most_common(1)[0][1],
         cz.most_common()[-1][0], cz.most_common()[-1][1]))
print('覆盖卦数:', len(cz), '/64')
