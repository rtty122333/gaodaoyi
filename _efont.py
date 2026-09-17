# -*- coding: utf-8 -*-
"""
efont (M5GFX u8g2 格式) 读取器 —— 复刻 lgfx_fonts.cpp 中 U8g2font 的查找与位图解码。
用来在 PC 上：① 判断缺字 ② 取出字形参数(宽高/偏移/步进) ③ 还原点阵(做预览)
"""
import re

EF = 'D:/cardputer/gddy/.pio/libdeps/gddy/M5GFX/src/lgfx/Fonts/efont/'
_CACHE = {}


def _unescape(ch):
    out = bytearray()
    i = 0
    while i < len(ch):
        c = ch[i]
        if c == 0x5C:
            i += 1
            n = ch[i]
            if 0x30 <= n <= 0x37:
                j = i; k = 0
                while k < 3 and j < len(ch) and 0x30 <= ch[j] <= 0x37:
                    j += 1; k += 1
                out.append(int(ch[i:j], 8) & 0xFF); i = j; continue
            if n in (0x78, 0x58):
                j = i + 1
                while j < len(ch) and chr(ch[j]) in '0123456789abcdefABCDEF':
                    j += 1
                out.append(int(ch[i + 1:j], 16) & 0xFF); i = j; continue
            out.append({0x6E: 10, 0x74: 9, 0x72: 13}.get(n, n)); i += 1
        else:
            out.append(c); i += 1
    return out


def load(sym):
    """只读取目标数组那一段(mmap 定位), 不把 17MB 的 .c 整个读进来"""
    if sym in _CACHE:
        return _CACHE[sym]
    import mmap
    fam = sym.split('_')[2]                    # cn / tw / ja / kr
    path = EF + 'lgfx_efont_%s.c' % fam
    with open(path, 'rb') as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        i = mm.find(b'const uint8_t ' + sym.encode() + b'[')
        if i < 0:
            raise RuntimeError('找不到数组 ' + sym)
        m = re.compile(rb'\[(\d+)\]\s*=\s*').match(mm, mm.find(b'[', i))
        declared = int(m.group(1))
        pos = m.end()
        out = bytearray()
        while len(out) < declared:
            q = mm.find(b'"', pos)
            if q < 0:
                break
            j = q + 1
            while True:                        # 找未被转义的结束引号
                j = mm.find(b'"', j)
                if j < 0:
                    break
                k, nb = j - 1, 0
                while mm[k] == 0x5C:
                    nb += 1; k -= 1
                if nb % 2 == 0:
                    break
                j += 1
            out += _unescape(mm[q + 1:j])
            pos = j + 1
        mm.close()
    data = bytes(out[:declared])
    _CACHE[sym] = data
    return data


class Font(object):
    def __init__(self, sym=None, raw=None):
        self.sym = sym
        self.d = bytes(raw) if raw is not None else load(sym)
        d = self.d
        self.glyph_cnt = d[0]
        self.bp0, self.bp1 = d[2], d[3]
        self.bpcw, self.bpch = d[4], d[5]
        self.bpcx, self.bpcy, self.bpcdx = d[6], d[7], d[8]
        self.max_w, self.max_h = d[9], d[10]
        # x_offset / y_offset 在 u8g2 头里是「有符号字节」(efont 为 0xFE = -2)
        self.x_offset = d[11] - 256 if d[11] > 127 else d[11]
        self.y_offset = d[12] - 256 if d[12] > 127 else d[12]
        self.upA = (d[17] << 8) | d[18]
        self.loA = (d[19] << 8) | d[20]
        self.uni = (d[21] << 8) | d[22]

    # ---- 复刻 getGlyph ----
    def rec(self, cp):
        d = self.d
        if cp <= 255:
            f = 23
            if cp >= 0x61:
                f += self.loA
            elif cp >= 0x41:
                f += self.upA
            while f + 1 < len(d) and d[f + 1]:
                if d[f] == cp:
                    return f + 2
                f += d[f + 1]
            return None
        f = 23 + self.uni
        lut = f
        guard = 0
        while True:
            if lut + 3 >= len(d):
                return None
            skip = (d[lut] << 8) | d[lut + 1]
            e = (d[lut + 2] << 8) | d[lut + 3]
            f += skip
            lut += 4
            guard += 1
            if not (e < cp) or guard > 300000:
                break
        while f + 3 < len(d):
            e = (d[f] << 8) | d[f + 1]
            if e == 0:
                return None
            if e == cp:
                return f + 3
            if d[f + 2] == 0:          # 长度 0 无法前进: 只可能是数据坏了, 直接判为无此字
                return None
            f += d[f + 2]
        return None

    def has(self, cp):
        return self.rec(cp) is not None

    def cps(self):
        """一次性枚举字库覆盖的全部码点(走 LUT + 各块链表), 比逐字 has() 快几个数量级"""
        if getattr(self, '_cps', None) is not None:
            return self._cps
        d = self.d
        out = set()
        # ASCII 链表
        f = 23
        while f + 1 < len(d) and d[f + 1]:
            out.add(d[f]); f += d[f + 1]
        # unicode: LUT 每条 4 字节 [skip_hi,skip_lo,cp_hi,cp_lo]
        lut0 = 23 + self.uni
        if lut0 + 4 <= len(d):
            step = (d[lut0] << 8) | d[lut0 + 1]        # 第一条的 skip = 整张表长度
            n = step // 4
            f = lut0                                   # 与 getGlyph 一样累加 skip
            for i in range(n):
                o = lut0 + i * 4
                if o + 3 >= len(d):
                    break
                f += (d[o] << 8) | d[o + 1]
                g = f
                guard = 0
                while g + 3 < len(d):
                    e = (d[g] << 8) | d[g + 1]
                    if e == 0:
                        break
                    out.add(e)
                    g += d[g + 2]
                    guard += 1
                    if guard > 7000:
                        break
        self._cps = out
        return out

    # ---- 复刻 u8g2_font_decode_t (LSB-first 位流) ----
    def _bits(self, off, pos, cnt):
        v = 0
        for k in range(cnt):
            bit = pos + k
            v |= ((self.d[off + (bit >> 3)] >> (bit & 7)) & 1) << k
        return v

    def glyph(self, cp):
        """返回 (w, h, xoff, yoff, dx, pixels[list of 0/1]) 或 None"""
        off = self.rec(cp)
        if off is None:
            return None
        pos = 0
        w = self._bits(off, pos, self.bpcw); pos += self.bpcw
        h = self._bits(off, pos, self.bpch); pos += self.bpch
        xo = self._bits(off, pos, self.bpcx) - (1 << (self.bpcx - 1)); pos += self.bpcx
        yo = self._bits(off, pos, self.bpcy) - (1 << (self.bpcy - 1)); pos += self.bpcy
        dx = self._bits(off, pos, self.bpcdx) - (1 << (self.bpcdx - 1)); pos += self.bpcdx

        px = []
        lx = ly = 0
        for _ in range(200):
            if ly >= h:
                break
            z = self._bits(off, pos, self.bp0); pos += self.bp0
            o = self._bits(off, pos, self.bp1); pos += self.bp1
            i = 0
            while True:
                length = z if i == 0 else o
                while length:
                    take = min(length, w - lx) if w > lx else length
                    for _k in range(take):
                        px.append(i)
                    lx += take
                    length -= take
                    if lx >= w:
                        lx = 0
                        ly += 1
                i ^= 1
                if i:
                    continue
                if self._bits(off, pos, 1):
                    pos += 1
                    continue
                pos += 1
                break
        return w, h, xo, yo, dx, px[:w * h]


# ---- 用来把字画到一条扫描线上(做整屏预览) ----
class Canvas(object):
    """1bpp 画布; draw_text 复刻 U8g2font::drawChar 的定位公式"""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0] * w for _ in range(h)]

    def rect(self, x, y, w, h, v=1):
        for j in range(max(0, y), min(self.h, y + h)):
            for i in range(max(0, x), min(self.w, x + w)):
                self.px[j][i] = v

    def char(self, f, cp, x, y, metrics_y_offset=None):
        # M5GFX: y = 行顶; baseline = y + (max_char_height + y_offset); 字形顶 = baseline - y_bits - h
        g = f.glyph(cp)
        if g is None:
            return f.max_w
        w, h, xo, yo, dx, pix = g
        x += xo
        baseline = y + (f.max_h + f.y_offset)
        ytop = baseline - yo - h
        for r in range(h):
            for c in range(w):
                if pix[r * w + c]:
                    yy, xx = ytop + r, x + c
                    if 0 <= yy < self.h and 0 <= xx < self.w:
                        self.px[yy][xx] = 1
        return dx

    def text(self, f, s, x, y):
        for ch in s:
            x += self.char(f, ord(ch), x, y)
        return x

    def save(self, path, scale=1, bg=0):
        try:
            from PIL import Image
        except ImportError:
            return False
        im = Image.new('L', (self.w, self.h), 255 if bg == 0 else 0)
        for r in range(self.h):
            for c in range(self.w):
                v = self.px[r][c]
                im.putpixel((c, r), (255 if v else 0) if bg == 0 else (0 if v else 255))
        if scale > 1:
            im = im.resize((self.w * scale, self.h * scale), Image.NEAREST)
        im.save(path)
        return True
