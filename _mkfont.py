# -*- coding: utf-8 -*-
"""
生成「补字字体」src/sup_font.h

背景：M5GFX 自带的 efont 简体中文字库只收了 GB2312 那一档（约 7500 字），
      《高岛易断》正文里有 160+ 个生僻字（姤、夬、禴、繘…）它没有，
      M5GFX 遇到缺字会调 drawCharDummy 画一个方块 —— 就是屏幕上那些"□"。

做法：把缺字从系统宋体(SimSun)的 12px / 14px 点阵里取出来，编码成 u8g2 格式字体，
      作为「第二字体」给 M5GFX 用。宋体点阵与 efont 的形状几乎逐像素一致，
      几何关系实测为：  efont.w  = 宋体 ink 宽度，
                        efont.yo = (ascent - ink 顶行) - ink 高度，
                        efont.xo = 宋体 ink 左列(k0) + 偏置
      （偏置逐字号不同：14px 是 +1，12px 是 +0；抽样 25 个常用字，多数完全逐像素吻合，
        其余仅差 ±1px 的 hinting 差异）

为什么做 12 和 14 两套：UI 里用了 efontCN_12 和 efontCN_14 两种字号，
      两者缺字集合相同，所以两套都要补，否则 12px 的那几行仍会出现方块。

用法: python _mkfont.py          (需要 Pillow；本机用隔离环境的 python)
产物: src/sup_font.h
"""
import os
import sys
import io

sys.path.insert(0, 'D:/cardputer/gddy')
from _efont import Font
from PIL import Image, ImageFont

NATIVE = 'C:/Windows/Fonts/simsun.ttc'     # 宋体(基础档, 覆盖 GB2312 及常用扩展)
EXTB   = 'C:/Windows/Fonts/simsunb.ttf'    # 宋体-扩展B(补 CJK 扩展 B 区的非 BMP 生僻字)
FACES  = [NATIVE, EXTB]                    # 按序回退: 前一个取不到字模就换下一个
PUA_BASE = 0xE000                          # 非 BMP 字符借用的私用区码点

SRC_FILES = ['D:/cardputer/gddy/src/gddy_data.h',
             'D:/cardputer/gddy/src/main.cpp']

# (后缀, efont 符号, ppem, 点阵宽上限, 步进, xo 偏置)
# xo 偏置: 实测 efontCN_14 的 xo = 宋体墨迹左列 k0 + 1, 而 efontCN_12 的 xo = k0
#          (两档字号的生成管线不同, 差这 1px)
SIZES = [
    ('14', 'lgfx_efont_cn_14', 14, 13, 14, 1),
    ('12', 'lgfx_efont_cn_12', 12, 11, 12, 0),
]

report = io.StringIO()
def P(*a):
    print(*a, file=report)

def log(*a):
    print(*a, flush=True)


# ---------------- u8g2 字体编码 ----------------
def bitstream(pairs):
    """[(value, bits), ...] -> LSB-first 字节流"""
    bits = []
    for v, n in pairs:
        for k in range(n):
            bits.append((v >> k) & 1)
    out = bytearray()
    for i in range(0, len(bits), 8):
        b = 0
        for k, x in enumerate(bits[i:i + 8]):
            b |= x << k
        out.append(b)
    return bytes(out)


def encode_glyph(w, h, xo, yb, dx, rows):
    """行按 '#'/'.' 给出 -> 一条 u8g2 字形记录的数据部分"""
    put = []
    # w/h 无符号; xo/yb/dx 为有符号 5bit, M5GFX 解码时用 get_signed_bits():
    #   value = raw - (1 << (cnt-1))   =>  编码必须写 raw = value + 16
    put += [(w, 4), (h, 4), ((xo + 16) & 31, 5), ((yb + 16) & 31, 5), ((dx + 16) & 31, 5)]
    px = []
    for r in range(h):
        row = rows[r]
        for c in range(w):
            px.append(1 if row[c] == '#' else 0)
    # 线性 RLE, 交替 0-run / 1-run, 以 0-run 开头
    runs = []
    cur, n = 0, 0
    for p in px:
        if p == cur:
            n += 1
        else:
            runs.append((cur, n)); cur, n = p, 1
    runs.append((cur, n))
    if runs[0][0] != 0:
        runs.insert(0, (0, 0))
    if len(runs) % 2:
        runs.append((1, 0))
    # 逐对输出: (0-run, 1-run, 结束标志0)
    for i in range(0, len(runs), 2):
        z, o = runs[i][1], runs[i + 1][1]
        assert z <= 255 and o <= 255, 'run 太长: %d/%d' % (z, o)
        put += [(z, 8), (o, 8), (0, 1)]
    return bitstream(put)


def build_font(glyphs, max_w, max_h):
    """glyphs: [(cp, w, h, xo, yb, dx, rows), ...] (按 cp 升序) -> 字体字节流"""
    n = len(glyphs)
    blocks = []
    for cp, w, h, xo, yb, dx, rows in glyphs:
        gd = encode_glyph(w, h, xo, yb, dx, rows)
        rec_len = 3 + len(gd)          # 记录长度字段 = 整条记录长(cp 2B + 长度 1B + 数据)
        assert rec_len <= 255, 'U+%04X 记录 %d 字节, 超过 1 字节长度域' % (cp, rec_len)
        blocks.append(bytes([cp >> 8, cp & 0xFF, rec_len]) + gd + b'\x00\x00')

    tbl = (n + 1) * 4
    lut = bytearray()
    for i in range(n):
        skip = tbl if i == 0 else len(blocks[i - 1])
        lut += bytes([skip >> 8, skip & 0xFF, glyphs[i][0] >> 8, glyphs[i][0] & 0xFF])
    lut += bytes([len(blocks[-1]) >> 8, len(blocks[-1]) & 0xFF, 0xFF, 0xFF])   # 哨兵

    hdr = bytes([
        min(n, 255),        # [0]  glyph_cnt (cp>255 走 unicode 表, 此处不参与查找)
        0,                  # [1]  bbx_mode
        8, 8,               # [2-3] bits_per_0 / bits_per_1  (run 最长 255, 够用)
        4, 4,               # [4-5] bits_per_char_width / height
        5, 5, 5,            # [6-8] bits_per_char_x / y / delta_x
        max_w, max_h,       # [9-10] max_char_width / height (与对应 efont 一致)
        0, 0xFE,            # [11-12] x_offset / y_offset(-2, 与 efont 一致)
        9, 0xFE, 10, 0xFF,  # [13-16] ascent_A / descent_g / ascent_para / descent_para
        0, 2,               # [17-18] start_pos_upper_A
        0, 2,               # [19-20] start_pos_lower_a
        0, 8,               # [21-22] start_pos_unicode → 表在 23+8=31
    ])
    # 23..30: 8 个 0 —— 23/24 空置, 25/26 构成空的 ASCII 链表头(font[1]==0 → 直接返回 nullptr),
    # 这样 unicode 码表正好落在 23 + start_pos_unicode(8) = 31 处(与 M5GFX 的算法一致)
    return hdr + b'\x00' * 8 + bytes(lut) + b''.join(blocks) + b'\x00\x00'


# ---------------- 取字模 ----------------
_FACE_CACHE = {}
def faces_for(ppem):
    if ppem not in _FACE_CACHE:
        _FACE_CACHE[ppem] = [ImageFont.truetype(p, ppem, index=0) for p in FACES]
    return _FACE_CACHE[ppem]


def render_em(fs, real):
    """把 chr(real) 按该字号渲染到 32x32 画布(坐标 = 相对文字原点)。
    按序尝试各字体, 返回 (pixels, 命中的字体序号); 都取不到则 (None, -1)。"""
    for fi, f in enumerate(fs):
        try:
            core, (ox, oy) = f.getmask2(chr(real), mode='1')
            mw, mh = core.size
        except Exception:
            continue
        if mw == 0 or mh == 0:
            continue
        img = Image.frombytes('L', (mw, mh), bytes(core))
        canvas = Image.new('L', (32, 32), 0)
        sx0, sy0 = max(0, -ox), max(0, -oy)
        if sx0 or sy0:
            img = img.crop((sx0, sy0, mw, mh))
        canvas.paste(img, (max(0, ox), max(0, oy)))
        px = canvas.load()
        if any(px[c, r] > 127 for c in range(32) for r in range(32)):
            return px, fi
    return None, -1


def ink_box(px):
    """画布 -> (k0,k1,r0,r1) 墨迹框, 无墨迹返回 None"""
    cols = [c for c in range(32) if any(px[c, r] > 127 for r in range(32))]
    rows_ = [r for r in range(32) if any(px[c, r] > 127 for c in range(32))]
    if not cols or not rows_:
        return None
    return cols[0], cols[-1], rows_[0], rows_[-1]


def build_one(label, efont_sym, ppem, max_w, adv, xo_bias, refs):
    """为一档字号生成字体。refs: [(cp_in_font, 原码点), ...]"""
    fs = faces_for(ppem)
    ascent = fs[0].getmetrics()[0]
    ef = Font(efont_sym)
    log('[%s] 取字模 / 编码' % label)

    glyphs, bad, fallback_used = [], [], []
    for ref, real in refs:
        px, fi = render_em(fs, real)
        if px is None:
            bad.append((real, '各字体均无字模')); continue
        if fi:
            fallback_used.append((real, fi))
        box = ink_box(px)
        if box is None:
            bad.append((real, '墨迹为空')); continue
        k0, k1, r0, r1 = box
        iw, ih = k1 - k0 + 1, r1 - r0 + 1
        w, h = iw, ih
        xo = k0 + xo_bias                     # efont 约定: 墨迹左列 + 偏置
        yb = (ascent - r0) - h               # efont 约定: 基线相对墨迹顶
        if iw > max_w or ih > 15 or not (-16 <= yb <= 15):
            bad.append((real, '越界 ink=%dx%d xo=%d yb=%d' % (iw, ih, xo, yb)))
        bits = [''.join('#' if px[k0 + c, r0 + r] > 127 else '.' for c in range(w))
                for r in range(h)]
        glyphs.append((ref, w, h, xo, yb, adv, bits))

    glyphs.sort(key=lambda g: g[0])
    font = build_font(glyphs, max_w, ppem)
    P('[%s] 字体: %d 字形, %d 字节' % (label, len(glyphs), len(font)))
    if fallback_used:
        P('     回退到 %s: %s' % (os.path.basename(EXTB),
                                 ' '.join('U+%X' % cp for cp, _ in fallback_used)))
    if bad:
        P('     !! 有问题 %d 个:' % len(bad))
        for cp, why in bad:
            P('        U+%X %s' % (cp, why))

    # ---- 回验: 独立解码器读回生成字体, 与原始点阵逐一比对 ----
    chk = Font(raw=font)
    errs = 0
    for cp, w, h, xo, yb, dx, bits in glyphs:
        g = chk.glyph(cp)
        if g is None:
            P('     !! 回读不到 U+%X' % cp); errs += 1; continue
        w2, h2, xo2, yb2, dx2, pix = g
        if (w2, h2, xo2, yb2, dx2) != (w, h, xo, yb, dx):
            P('     !! 参数不符 U+%X %s vs %s' % (cp, (w, h, xo, yb, dx), (w2, h2, xo2, yb2, dx2)))
            errs += 1; continue
        for r in range(h):
            got = ''.join('#' if pix[r * w + c] else '.' for c in range(w))
            if got != bits[r]:
                P('     !! 点阵不符 U+%X 行%d\n        want %s\n        got  %s' % (cp, r, bits[r], got))
                errs += 1; break
    for cp in (0x4E00, 0x9AD8, 0x0041, 0x7F00):        # 不该命中的码点必须查不到
        if chk.glyph(cp) is not None:
            P('     !! U+%04X 不该命中却命中' % cp); errs += 1
    P('     回验: %s' % ('全部通过' if errs == 0 else '%d 处失败' % errs))

    # ---- 与 efont 对照(仅报告, 不影响产物): 两边都有的字, 几何应当接近 ----
    exact = tot = 0
    worst = []
    for ch in '一中大天人上下有之乾元亨利象曰明夷屯蒙需讼师比乾坤':
        cp = ord(ch)
        g = ef.glyph(cp)
        if not g:
            continue
        px, _ = render_em(fs, cp)
        box = ink_box(px) if px else None
        if box is None:
            continue
        k0, k1, r0, r1 = box
        mine = (k0 + xo_bias, k1 - k0 + 1, (ascent - r0) - (r1 - r0 + 1))
        tot += 1
        d = max(abs(mine[0] - g[2]), abs(mine[1] - g[0]), abs(mine[2] - g[3]))
        if d == 0:
            exact += 1
        else:
            worst.append('%s(差%d)' % (ch, d))
    if tot:
        P('     与 efont 抽样对照: %d/%d 完全一致%s' %
          (exact, tot, ('; 其余 ' + ' '.join(worst)) if worst else ''))
    return font, glyphs, bad


# ---------------- 输出 ----------------
def emit_header(path, results, extra):
    def hexdump(b, per=20):
        lines = []
        for i in range(0, len(b), per):
            lines.append('  ' + ','.join('0x%02X' % x for x in b[i:i + per]) + ',')
        return '\n'.join(lines)

    h = []
    h.append('// 自动生成, 请勿手改 —— 由 _mkfont.py 从系统宋体点阵生成')
    h.append('// M5GFX 自带 efont 简体字库缺的 %d 个码点(姤 之类生僻字)在此补齐,' % len(results[0][1]))
    h.append('// 由 main.cpp 的 drawText() 在缺字时逐字切换到对应字号的补字字体绘制。')
    h.append('#pragma once')
    h.append('#include <stdint.h>')
    h.append('')
    h.append('// 非 BMP 字符 -> 借用的私用区码点(16 位码点装不下 4 字节 UTF-8 的码点, 只能借位)')
    h.append('#define SUP_NONBMP_COUNT %d' % len(extra))
    if extra:
        h.append('static const uint32_t SUP_NONBMP[SUP_NONBMP_COUNT][2] = {')
        for a, b in extra:
            h.append('  { 0x%X, 0x%04X },' % (a, b))
        h.append('};')
    else:
        h.append('static const uint32_t SUP_NONBMP[1][2] = { {0, 0} };')
    h.append('')

    for label, glyphs, font in results:
        h.append('// ---- %spx (对应 efontCN_%s) ----' % (label, label))
        h.append('#define SUP_COUNT_%s %d' % (label, len(glyphs)))
        h.append('#define SUP_BYTES_%s %d' % (label, len(font)))
        h.append('// 覆盖的码点(升序, 供二分查找)')
        h.append('static const uint16_t SUP_CPS_%s[SUP_COUNT_%s] = {' % (label, label))
        for i in range(0, len(glyphs), 12):
            h.append('  ' + ','.join('0x%04X' % g[0] for g in glyphs[i:i + 12]) + ',')
        h.append('};')
        h.append('static const uint8_t supFontData%s[SUP_BYTES_%s] = {' % (label, label))
        h.append(hexdump(font))
        h.append('};')
        h.append('')
    open(path, 'w', encoding='utf-8').write('\n'.join(h))


# ---------------- 预览 ----------------
def make_preview(results):
    from _efont import Canvas
    label, glyphs, font = results[0]
    sup = Font(raw=font)
    sup_set = set(g[0] for g in glyphs)

    cols, cell = 16, 20
    rows = (len(glyphs) + cols - 1) // cols
    sheet = Canvas(cols * cell, rows * cell)
    for i, (cp, w, h, xo, yb, dx, bits) in enumerate(glyphs):
        cx = (i % cols) * cell + 3
        cy = (i // cols) * cell + 3
        for r in range(h):
            for c in range(w):
                if bits[r][c] == '#':
                    sheet.px[cy + r][cx + c] = 1
    sheet.save('D:/cardputer/gddy/_sup_sheet.png', scale=3)
    print('字表: %d 字 -> _sup_sheet.png' % len(glyphs))

    # 屏幕模拟: 找一段含缺字的正文, 混排绘制
    ef = Font('lgfx_efont_cn_14')
    raw = open('D:/cardputer/gddy/src/gddy_data.h', encoding='utf-8').read()
    sample, pos = '', -1
    while True:
        pos = raw.find('姤', pos + 1)
        if pos < 0:
            break
        seg = raw[pos - 60:pos + 240]
        if seg.count('\\n') >= 2:
            sample = seg
            break
    if not sample:
        sample = raw[4000:4260]
    sample = sample.replace('\\n', '\n')
    lines, cur = [], ''
    for ch in sample:
        if ch == '\n':
            lines.append(cur); cur = ''; continue
        if ord(ch) < 0x20:
            continue
        cur += ch
        if len(cur) >= 16:
            lines.append(cur); cur = ''
    if cur:
        lines.append(cur)

    scr = Canvas(240, 135)
    X0, Y0, ROWH = 2, 18, 16
    for i, ln in enumerate(lines[:6]):
        y = Y0 + i * ROWH
        x = X0
        for ch in ln:
            cp = ord(ch)
            x += scr.char(sup if cp in sup_set else ef, cp, x, y)
    scr.save('D:/cardputer/gddy/_sup_screen.png', scale=3)
    print('屏幕模拟 -> _sup_screen.png')

    # efont vs 补字 对比条
    cps = [0x59E4, 0x5939, 0x6C59, 0x8C5C, 0x9A56, 0xE000, 0xE001]
    cv = Canvas(2 + len(cps) * 16, 40)
    for i, cp in enumerate(cps):
        cv.char(ef, cp, 2 + i * 16, 3)
        cv.char(sup, cp, 2 + i * 16, 21)
    cv.save('D:/cardputer/gddy/_cmp_jiugou.png', scale=7)
    print('对比条 -> _cmp_jiugou.png (上行 efont / 下行 补字)')


def main():
    log('[1] 扫描源码码点')
    used = set()
    for p in SRC_FILES:
        used |= set(ord(c) for c in open(p, encoding='utf-8').read())
    all_cps = sorted(cp for cp in used if cp >= 0x80)

    ef14 = Font(SIZES[0][1])
    covered = ef14.cps()
    missing = [cp for cp in all_cps if cp not in covered]
    bmp = [cp for cp in missing if cp <= 0xFFFF]
    ext = [cp for cp in missing if cp > 0xFFFF]
    log('[2] 源码 %d 个非 ASCII 码点; efont 缺 %d (BMP %d + 非BMP %d)'
        % (len(all_cps), len(missing), len(bmp), len(ext)))
    P('源码非 ASCII 码点 %d 个; efont 缺字 %d (BMP %d + 非BMP %d)'
      % (len(all_cps), len(missing), len(bmp), len(ext)))
    P('非 BMP: ' + ' '.join('U+%X' % c for c in ext))

    # 非 BMP 借私用区码点，BMP 原样
    refs = [(cp, cp) for cp in bmp] + [(PUA_BASE + k, cp) for k, cp in enumerate(ext)]
    extra = [(cp, PUA_BASE + k) for k, cp in enumerate(ext)]

    results = []
    for label, sym, ppem, max_w, adv, xo_bias in SIZES:
        font, glyphs, bad = build_one(label, sym, ppem, max_w, adv, xo_bias, refs)
        results.append((label, glyphs, font))

    log('[3] 写 sup_font.h')
    emit_header('D:/cardputer/gddy/src/sup_font.h', results, extra)
    P('已写出 src/sup_font.h')
    for label, glyphs, font in results:
        P('   %spx: %d 字形 / %d 字节' % (label, len(glyphs), len(font)))

    log('[4] 预览图')
    make_preview(results)

    open('D:/cardputer/gddy/_mkfont.out', 'w', encoding='utf-8').write(report.getvalue())
    print('done')


if __name__ == '__main__':
    main()
