# -*- coding: utf-8 -*-
"""
从 raw/NN.html 重建 gddy_data.h

每卦正文拆成 7 段: [0]=全卦总论, [1..6]=初爻/二爻/三爻/四爻/五爻/上爻
(乾/坤的"用九/用六"并入上爻段尾)

输出结构:
  struct HexSection { tab, name, body }
    tab  = 详情页标题用短标签 ("全卦" / "初九" / "九二" ...)
    name = 目录条目名 ("全卦总论" / "初九 潜龙勿用" ...)
    body = 正文, 用 \n 分段

用法: python D:/cardputer/gddy/_build.py
"""
import re, os, io, html, shutil

RAW_DIR = 'D:/cardputer/gddy/raw'
OUT_H   = 'D:/cardputer/gddy/src/gddy_data.h'
LOG     = 'D:/cardputer/gddy/_build.out'

log = io.StringIO()

# 正文容器: <div class="space-y-6 font-serif ...">
CONTAINER = re.compile(r'<div class="space-y-6 font-serif[^"]*">')
# 正文元素: h2/h3/h4 标题, p 段落, blockquote 引文(卦辞/彖象传), li 条目("占"下的问答)
# 注意: 站点引文用的是 <blockquote class="border-l-4 ...">, 不是 <b>;
#       而"占"小节的内容全在 <ul><li> 里 —— 这两类都不能漏, 否则成段丢字.
ELEM      = re.compile(r'<(h2|h3|h4|p|b|blockquote|li)\b[^>]*>(.*?)</\1>', re.S)
TAG       = re.compile(r'<[^>]+>')

YAO  = re.compile(r'^(初九|初六|九二|九三|九四|九五|六二|六三|六四|六五|上九|上六)\s*[:：]\s*(.*)$')
YONG = re.compile(r'^(用九|用六)')

# 噪声: 校勘编号 ［82］ / [146]
NOISE_IDX  = re.compile(r'[\[［]\s*\d+\s*[\]］]')
# 中文/全角标点之间的孤立空格 (站点排版残留)
CJK = r'\u4e00-\u9fff\u3000-\u303f\uff00-\uffef'
CJK_SPACE  = re.compile(r'(?<=[' + CJK + r'])[ \t]+(?=[' + CJK + r'])')

PUNCT_TAIL = '。，、；：！？　 '


def body_container(s):
    m = CONTAINER.search(s)
    if not m:
        return None
    start = m.end()
    depth = 1
    for mm in re.finditer(r'<div\b|</div>', s[start:]):
        depth += (-1 if mm.group(0) == '</div>' else 1)
        if depth == 0:
            return s[start:start + mm.start()]
    return s[start:]


def plain(t):
    t = re.sub(r'<br\s*/?>', '\n', t)
    t = TAG.sub('', t)
    t = html.unescape(t)
    t = t.replace('\u3000', ' ')
    t = NOISE_IDX.sub('', t)
    t = re.sub(r'[ \t]+', ' ', t)
    t = CJK_SPACE.sub('', t)
    return t.strip()


def parse_page(path):
    s = open(path, encoding='utf-8', errors='replace').read()
    frag = body_container(s)
    if frag is None:
        return None, None, 'no-container'
    elems = []
    for m in ELEM.finditer(frag):
        txt = plain(m.group(2))
        if txt:
            elems.append((m.group(1), txt))
    if not elems:
        return None, s, 'no-elems'
    return elems, s, None


def build_sections(elems):
    """-> [(tab, name, [line,...]) x7]"""
    pre = []                       # 第一个 h3 之前 = 全卦总论
    yao = []                       # [(rawtitle, [line,...])]
    cur = pre
    for tag, txt in elems:
        if tag == 'h3':
            yao.append([txt, []])
            cur = yao[-1][1]
        elif tag == 'h4':
            cur.append('【' + txt + '】')
        else:
            cur.append(txt)

    # 用九 / 用六 并入上一爻
    merged = []
    for raw, lines in yao:
        if YONG.match(raw) and merged:
            merged[-1][1].append('')
            merged[-1][1].append('【' + raw.rstrip('。') + '】')
            merged[-1][1].extend(lines)
        else:
            merged.append([raw, lines])
    yao = merged

    secs = [('全卦', '全卦总论', pre)]
    for raw, lines in yao:
        m = YAO.match(raw)
        if m:
            pos, verse = m.group(1), m.group(2).strip()
        else:
            pos, verse = raw[:2], raw[2:].strip()
        verse = verse.rstrip(PUNCT_TAIL)
        # 目录名保留完整爻题(不截断), 太长由设备端横向滚动显示
        name = ('%s %s' % (pos, verse[:18])).strip() or pos
        secs.append((pos, name, ['【' + raw.rstrip('。') + '】'] + lines))
    return secs


def cesc(t):
    t = t.replace('\\', '\\\\').replace('"', '\\"')
    return t.replace('\n', '\\n')


records, bad = [], []

for n in range(1, 65):
    path = os.path.join(RAW_DIR, '%02d.html' % n)
    elems, page, err = parse_page(path)
    if err:
        bad.append((n, err, 0))
        continue
    secs = build_sections(elems)
    h1 = re.search(r'<h1[^>]*>(.*?)</h1>', page, re.S)
    name = plain(h1.group(1)) if h1 else ''
    name = re.sub(r'^第\s*\d+\s*卦\s*', '', name).strip(' ·')
    name = name or ('卦%02d' % n)
    records.append((n, name, secs))
    if len(secs) != 7:
        bad.append((n, name, len(secs)))
    log.write("%2d %-6s " % (n, name))
    log.write(" | ".join(x[1] for x in secs) + "\n")

log.write("\n== records=%d  bad=%d ==\n" % (len(records), len(bad)))
for b in bad:
    log.write("!! %s\n" % (b,))

if bad:
    log.write("\n有异常页, 不写出 gddy_data.h\n")
    open(LOG, 'w', encoding='utf-8').write(log.getvalue())
    print("ABORT")
    raise SystemExit(1)

buf = io.StringIO()
buf.write('// Auto-generated 高岛易断 (Takashima Ekidan) data -- 64 hexagrams.\n')
buf.write('// Source: gaodaoyiduan.org (public-domain Meiji-era text). UTF-8.\n')
buf.write('// 每卦 7 段: [0]=全卦总论, [1..6]=初爻/二爻/三爻/四爻/五爻/上爻\n')
buf.write('// 重新生成: python D:/cardputer/gddy/_build.py\n')
buf.write('#ifndef GDDY_DATA_H\n#define GDDY_DATA_H\n\n')
buf.write('struct HexSection {\n')
buf.write('  const char* tab;    // 详情页短标签: 全卦/初九/九二...\n')
buf.write('  const char* name;   // 目录条目名\n')
buf.write('  const char* body;   // 正文, \\n 分段\n')
buf.write('};\n\n')
buf.write('struct Hexagram {\n  int num;\n  const char* name;\n  const HexSection secs[7];\n};\n\n')
buf.write('static const Hexagram HEXAGRAMS[64] = {\n')
for num, name, secs in records:
    buf.write('  {%d, "%s", {\n' % (num, name))
    for tab, sname, lines in secs:
        body = '\n'.join(lines)
        buf.write('    {"%s", "%s",\n     "%s"},\n' % (cesc(tab), cesc(sname), cesc(body)))
    buf.write('  }},\n')
buf.write('};\n\n#endif\n')

if os.path.exists(OUT_H):
    shutil.copyfile(OUT_H, OUT_H + '.bak')
open(OUT_H, 'w', encoding='utf-8', newline='\n').write(buf.getvalue())
log.write("WROTE %s  (%d chars, %d bytes)\n" % (
    OUT_H, len(buf.getvalue()), len(buf.getvalue().encode('utf-8'))))
open(LOG, 'w', encoding='utf-8').write(log.getvalue())
print("OK")
