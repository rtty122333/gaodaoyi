# -*- coding: utf-8 -*-
"""校验 gddy_data.h: 段数/标签/目录名宽度/正文非空"""
import re, io

s = open('D:/cardputer/gddy/src/gddy_data.h', encoding='utf-8').read()
out = io.StringIO()

REC = re.compile(r'\{"([^"]*)", "([^"]*)",\n     "((?:[^"\\]|\\.)*)"\}')
recs = REC.findall(s)
out.write("sections parsed = %d (expect 448)\n" % len(recs))

def width(t):
    """显示宽度(全角字位数)"""
    return sum(1 if ord(ch) > 0x2e80 else 0.5 for ch in t)

TABS = {'全卦', '初九', '初六', '九二', '九三', '九四', '九五',
        '六二', '六三', '六四', '六五', '上九', '上六'}
EXPECT = ['全卦', '初九/初六', '九二/六二', '九三/六三', '九四/六四', '九五/六五', '上九/上六']

problems = []
for i in range(0, len(recs), 7):
    group = recs[i:i + 7]
    if len(group) != 7:
        problems.append("hex %d group size %d" % (i // 7 + 1, len(group)))
        continue
    for k, (tab, name, body) in enumerate(group):
        if k == 0 and tab != '全卦':
            problems.append("hex%d sec0 tab=%s" % (i // 7 + 1, tab))
        if k > 0 and tab not in TABS:
            problems.append("hex%d sec%d bad tab=%s" % (i // 7 + 1, k, tab))
        if not body.strip():
            problems.append("hex%d sec%d EMPTY body" % (i // 7 + 1, k))
        # 目录名不再截断, 太长由设备横向滚动; 这里只挡异常超长
        if width(name) > 22:
            problems.append("hex%d sec%d name too long %.1f: %s" % (i // 7 + 1, k, width(name), name))
        # "占" 小节的问答条目必须还在 (曾经因漏解析 <li> 而整段丢失)
        if '问' not in body and '断曰' not in body and '占' not in body:
            problems.append("hex%d sec%d 疑似缺'占'内容: %s" % (i // 7 + 1, k, name))

# 每卦名字一致性
NAMES = re.findall(r'  \{(\d+), "([^"]*)", \{', s)
out.write("hexagrams = %d\n" % len(NAMES))
if len(NAMES) != 64:
    problems.append("hexagram count %d" % len(NAMES))

# 抽样: 乾卦七目 + 未济初六
first7 = recs[0:7]
out.write("\n[乾为天 七目]\n")
for tab, name, body in first7:
    out.write("  tab=%-4s name=%-12s body=%d字  首行: %s\n" %
              (tab, name, len(body), body.split('\\n')[0][:28]))

out.write("\n[火水未济 七目]\n")
for tab, name, body in recs[-7:]:
    out.write("  tab=%-4s name=%-12s body=%d字  首行: %s\n" %
              (tab, name, len(body), body.split('\\n')[0][:28]))

out.write("\nproblems = %d\n" % len(problems))
for p in problems[:25]:
    out.write("  !! " + p + "\n")

open('D:/cardputer/gddy/_verify.out', 'w', encoding='utf-8').write(out.getvalue())
print("done")
