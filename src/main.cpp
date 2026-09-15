/*
 * 高岛易断 · Cardputer 查询器
 * ------------------------------------------------
 * 三层浏览：
 *   一、选卦  开机列 64 卦编号+卦名，右侧显示该卦六爻图形
 *   二、详目  选定一卦后，列出 7 个条目 ——「全卦总论」+ 初/二/三/四/五/上六爻
 *             右侧卦象图中，当前选中的那一爻会高亮
 *   三、正文  所选条目的详细解读，可滚动
 *
 * 中文由 M5GFX 内置 efont 简体中文点阵字库渲染，无需外接字库。
 *
 * 操作：
 *   选卦态： 数字键输编号 -> Enter 打开；  Enter 打开当前选中
 *            上下键(; .)=移动一行   左右键(, /)=翻一整页
 *   详目态： 标题行只显示卦名(与右上电量框同一行)，下列 7 个条目，
 *            右侧卦象高亮当前条目对应的那一爻；
 *            条目名一行放不下时 —— 只有"当前选中"那条缓慢平滑滚动(逐像素,
 *            停-滚-停-回)，其余条目静止截断显示前几字 + 省略号
 *            上下键=选项移动         左右键=上一卦 / 下一卦
 *            Enter=进入正文          m=返回选卦
 *   正文态： 上下键=滚动一行         左右键=翻一整页
 *            m=返回详目
 *   (n/p 同"上下键"，保留兼容；方向键为键盘丝印箭头的第二功能键)
 *
 * 编译: pio run -d D:/cardputer/gddy
 * 烧录: pio run -d D:/cardputer/gddy -t upload
 * 数据: src/gddy_data.h 由 _build.py 从 raw/*.html 生成
 */
#include <M5Cardputer.h>
#include <lgfx/Fonts/efont/lgfx_efont_cn.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include "gddy_data.h"

// ---------- UI 常量 ----------
static const int ROW_H         = 16;    // 正文行高
static const int HEADER_H      = 18;    // 顶部标题行高度
static const int FOOTER_H      = 16;    // 底部提示条高度
static const int FOOTER_Y      = 135 - FOOTER_H;  // 底部提示条顶部 y
static const int CHARS_PER_LINE= 16;    // 正文每行最多全角字数
static const int LIST_ROWS     = 6;     // 选卦列表可见行数
static const int DETAIL_ROWS   = 6;     // 正文可见行数
static const int SEC_COUNT     = 7;     // 每卦条目数: 全卦总论 + 六爻
static const int SPLIT_X       = 160;   // 左文字区 / 右卦象区分栏线

// 配色
static const uint32_t C_BG     = 0x0000;
static const uint32_t C_TEXT   = 0xD6E0D6;
static const uint32_t C_TITLE  = 0xFFE0;
static const uint32_t C_HL     = 0x05A0;   // 选中行高亮底
static const uint32_t C_HL_TXT = 0x0000;
static const uint32_t C_HINT   = 0x6AC0;
static const uint32_t C_HEX    = 0xFFE0;   // 卦象线条色
static const uint32_t C_HEX_HL = 0xF800;   // 卦象中"当前爻"高亮色
static const uint32_t C_GRID   = 0x31A6;   // 分栏线

// ---------- 状态 ----------
enum View { SPLASH, LIST, TOC, DETAIL };
static View     view     = SPLASH;
static int      sel      = 0;     // 选卦: 当前选中卦 0..63
static int      listTop  = 0;     // 选卦: 首行索引
static int      tocSel   = 0;     // 详目: 当前条目 0..6
static int      secIdx   = 0;     // 正文: 正在看的条目号
static int      detailTop= 0;     // 正文: 首行索引
static String   inputBuf = "";    // 正在输入的编号
static std::vector<String> detailLines;
static unsigned long splashUntil = 0;
static bool      dirty    = true;
static int       g_tocRowH = 16;  // 详目行高(按字体实际高度算)
static int       g_tocY0   = 18;  // 详目首行 y

// 详目行内布局: 序号在 x=4, 名称从 x=18 起, 右边界 SPLIT_X-8
static const int TOC_NUM_X  = 4;
static const int TOC_NAME_X = 18;
static const int TOC_NAME_W = SPLIT_X - 8 - TOC_NAME_X;   // 名称可用像素宽

// 详目: 只有"当前选中"那条的名称做逐像素平滑滚动, 其余条目一律截断显示
enum TocPhase { TS_HOLD_HEAD, TS_ROLL, TS_HOLD_TAIL, TS_ROLL_BACK };
static TocPhase      g_tsPhase = TS_HOLD_HEAD;
static int           g_tsMax   = 0;    // 需要滚动的像素数(0 = 不滚)
static int           g_tsOff   = 0;    // 当前横向偏移(像素)
static unsigned long g_tsHold  = 0;    // 停留截止时刻
static unsigned long g_tsStep  = 0;    // 上次位移时刻
static const int     TOC_PX_STEP = 1;      // 每步位移(像素)
static const int     TOC_STEP_MS = 12;     // 位移间隔(小于主循环周期 -> 每帧走 1px, 约 45px/s)
static const unsigned long TOC_HOLD_HEAD = 1200;   // 开头停留
static const unsigned long TOC_HOLD_TAIL = 900;    // 末尾停留
static LGFX_Sprite*  g_tocSpr  = nullptr;          // 单行离屏缓冲(逐像素滚动的关键)
static uint16_t      g_slice[TOC_NAME_W * 20];     // 切片推送缓冲

// ---------- 卦象(六爻) ----------
// 八卦象征字 -> 三爻(bit0=初爻, 1=阳)
static int trigramSymbol(const char* s) {
  struct Sym { const char* k; int v; };
  static const Sym T[] = {
    {"乾", 7}, {"天", 7}, {"坤", 0}, {"地", 0},
    {"震", 1}, {"雷", 1}, {"巽", 6}, {"风", 6},
    {"坎", 2}, {"水", 2}, {"离", 5}, {"火", 5},
    {"艮", 4}, {"山", 4}, {"兑", 3}, {"泽", 3},
  };
  for (const Sym& t : T) if (memcmp(s, t.k, 3) == 0) return t.v;
  return 0;
}

// 由卦名求六爻: bit0..bit5 = 初爻..上爻 (1=阳爻)
static int hexagramLines(const char* name) {
  if (memcmp(name + 3, "为", 3) == 0) {      // 八纯卦(乾为天/坤为地...): 上下同
    int t = trigramSymbol(name);
    return t | (t << 3);
  }
  int upper = trigramSymbol(name);           // 第1字 = 上卦(外卦)
  int lower = trigramSymbol(name + 3);       // 第2字 = 下卦(内卦)
  return lower | (upper << 3);
}

// ---------- UTF-8 按字数折行 ----------
void buildLines(const char* text, std::vector<String>& out, int maxChars) {
  out.clear();
  String cur;
  int cp = 0;
  const char* p = text;
  while (*p) {
    unsigned char c = (unsigned char)*p;
    int clen = 1;
    if      (c >= 0xF0) clen = 4;
    else if (c >= 0xE0) clen = 3;
    else if (c >= 0xC0) clen = 2;
    if (c == '\n') {                 // 段落换行
      out.push_back(cur); cur = ""; cp = 0; p += clen; continue;
    }
    cur.concat(p, clen);             // 追加整个码点字节
    p += clen;
    if (++cp >= maxChars) {          // 达到行宽则断行
      out.push_back(cur); cur = ""; cp = 0;
    }
  }
  if (cur.length()) out.push_back(cur);
}

static void drawTocRow(int i);   // 前置声明: 滚动时只重绘这一行

// ---------- UTF-8 小工具 (目录名横向滚动用) ----------
static int utf8Len(unsigned char c) {
  if (c >= 0xF0) return 4;
  if (c >= 0xE0) return 3;
  if (c >= 0xC0) return 2;
  return 1;
}

static int cpCount(const char* s) {              // 码点个数
  int n = 0;
  for (const char* p = s; *p; p += utf8Len((unsigned char)*p)) ++n;
  return n;
}

// efont 全角字宽 = 字号(14px), 半角(ASCII) = 7px
static int glyphPx(unsigned char c) { return (c >= 0x80) ? 14 : 7; }

static int visibleCps(const char* s, int maxPx) { // 从头起能放下几个码点
  int n = 0, px = 0;
  for (const char* p = s; *p; ) {
    int w = glyphPx((unsigned char)*p);
    if (px + w > maxPx) break;
    px += w;
    ++n;
    p += utf8Len((unsigned char)*p);
  }
  return n;
}

// 取字符串开头的 n 个码点
static String firstCps(const char* s, int n) {
  String out;
  int i = 0;
  for (const char* p = s; *p && i < n; ++i) {
    int l = utf8Len((unsigned char)*p);
    out.concat(p, l);
    p += l;
  }
  return out;
}

// 截断显示: 放不下时保留前若干字 + 省略号 (未选中条目用)
static String truncName(const char* nm, int maxPx) {
  int total = cpCount(nm);
  int fit   = visibleCps(nm, maxPx);
  if (fit >= total) return String(nm);
  int keep  = visibleCps(nm, maxPx - 14);     // 让出一个全角位给省略号
  if (keep < 2) keep = fit;
  if (keep < 2) keep = 1;
  return firstCps(nm, keep) + "…";
}

// 重置选中条目的滚动时序: 回到最左, 先停一会儿再开始滚
static void tocScrollReset() {
  g_tsPhase = TS_HOLD_HEAD;
  g_tsOff   = 0;
  g_tsMax   = 0;
  g_tsHold  = millis();
  g_tsStep  = millis();
  if (!g_tocSpr) return;

  // 用离屏缓冲实测渲染宽度(比按字数估算准), 再算需要滚多少像素
  const char* nm = HEXAGRAMS[sel].secs[tocSel].name;
  g_tocSpr->setFont(&lgfx::fonts::efontCN_14);
  g_tocSpr->setTextWrap(false);
  g_tocSpr->setCursor(0, 0);
  g_tocSpr->print(nm);
  int w   = g_tocSpr->getCursorX();
  if (w <= 0) {                               // 极端兜底: 按字宽估算
    w = 0;
    for (const char* p = nm; *p; p += utf8Len((unsigned char)*p))
      w += glyphPx((unsigned char)*p);
  }
  int lim = g_tocSpr->width() - TOC_NAME_W;   // 缓冲宽度上限
  int over = (w > TOC_NAME_W) ? (w - TOC_NAME_W) : 0;
  if (over > lim) over = lim;
  g_tsMax = over;
}

// 每帧推进(只在选中行上滚动), 节奏: 停 -> 滚到尾 -> 停 -> 滚回 -> 循环
static void tocScrollTick() {
  if (g_tsMax <= 0) return;
  unsigned long now = millis();
  switch (g_tsPhase) {
    case TS_HOLD_HEAD:
      if (now - g_tsHold < TOC_HOLD_HEAD) return;
      g_tsPhase = TS_ROLL;
      g_tsStep  = now;
      break;
    case TS_ROLL:
      if (now - g_tsStep < TOC_STEP_MS) return;
      g_tsStep = now;
      g_tsOff += TOC_PX_STEP;
      if (g_tsOff >= g_tsMax) {                 // 到底: 停在尾部给人读
        g_tsOff  = g_tsMax;
        g_tsPhase = TS_HOLD_TAIL;
        g_tsHold = now;
      }
      drawTocRow(tocSel);
      break;
    case TS_HOLD_TAIL:
      if (now - g_tsHold < TOC_HOLD_TAIL) return;
      g_tsPhase = TS_ROLL_BACK;
      g_tsStep  = now;
      break;
    case TS_ROLL_BACK:
      if (now - g_tsStep < TOC_STEP_MS) return;
      g_tsStep = now;
      g_tsOff -= TOC_PX_STEP;
      if (g_tsOff <= 0) {                       // 回到头部: 再停一会儿
        g_tsOff  = 0;
        g_tsPhase = TS_HOLD_HEAD;
        g_tsHold = now;
      }
      drawTocRow(tocSel);
      break;
  }
}

// ---------- 视图切换 ----------
void openToc(int idx) {                  // 选卦 -> 详目
  if (idx < 0 || idx > 63) return;
  sel = idx;
  tocSel = 0;
  listTop = 0;
  tocScrollReset();
  view = TOC;
  dirty = true;
}

void openSection(int k) {                // 详目 -> 正文
  if (k < 0 || k >= SEC_COUNT) return;
  secIdx = k;
  detailTop = 0;
  buildLines(HEXAGRAMS[sel].secs[k].body, detailLines, CHARS_PER_LINE);
  view = DETAIL;
  dirty = true;
}

void clampListTop() {
  if (sel < listTop) listTop = sel;
  if (sel >= listTop + LIST_ROWS) listTop = sel - LIST_ROWS + 1;
  if (listTop < 0) listTop = 0;
}

// ---------- 滚动控制 ----------
void lineUp() {                                   // 上移一行 / 上移一项
  if (view == LIST)      { if (sel > 0) --sel; clampListTop(); }
  else if (view == TOC)  { if (tocSel > 0) { --tocSel; tocScrollReset(); } }
  else                   { if (detailTop > 0) --detailTop; }
  dirty = true;
}
void lineDown() {                                 // 下移一行 / 下移一项
  if (view == LIST)      { if (sel < 63) ++sel; clampListTop(); }
  else if (view == TOC)  { if (tocSel < SEC_COUNT - 1) { ++tocSel; tocScrollReset(); } }
  else                   { ++detailTop; }
  dirty = true;
}
void pageUp() {                                   // 左键
  if (view == LIST)      { sel -= LIST_ROWS; if (sel < 0) sel = 0; clampListTop(); }
  else if (view == TOC)  { if (sel > 0) { --sel; tocSel = 0; tocScrollReset(); } }  // 上一卦
  else                   { detailTop -= DETAIL_ROWS; if (detailTop < 0) detailTop = 0; }
  dirty = true;
}
void pageDown() {                                 // 右键
  if (view == LIST)      { sel += LIST_ROWS; if (sel > 63) sel = 63; clampListTop(); }
  else if (view == TOC)  { if (sel < 63) { ++sel; tocSel = 0; tocScrollReset(); } } // 下一卦
  else                   { detailTop += DETAIL_ROWS; }
  dirty = true;
}

// ---------- 绘制 ----------
void drawBattery() {
  int lvl = M5.Power.getBatteryLevel();
  if (lvl < 0) lvl = 0;
  if (lvl > 100) lvl = 100;

  // 注意: isCharging() 返回的是枚举 is_discharging(0)/is_charging(1)/charge_unknown(2),
  // 绝不能当 bool 用 —— charge_unknown(2) 会被误判成 true 导致闪电常亮。
  bool chg = (M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging);

  // 电池框 + 正极：始终显示（手机风格），状态画在框内
  int bx = 206, by = 1, bw = 32, bh = 16;
  int cx = bx + bw / 2;
  M5Cardputer.Display.drawRoundRect(bx, by, bw, bh, 3, C_HINT);
  M5Cardputer.Display.fillRect(bx + bw, by + 5, 2, 6, C_HINT);

  if (chg) {
    // 充电中：框内一个折线闪电（多段加粗线，M5GFX 无 fillPolygon）
    uint32_t bolt = 0xFFE0;
    int32_t lx[4] = { cx + 3, cx - 3, cx + 2, cx - 2 };
    int32_t ly[4] = { by + 2, by + 8, by + 8, by + 14 };
    for (int d = -1; d <= 1; d++) {
      for (int k = 0; k < 3; k++) {
        M5Cardputer.Display.drawLine(lx[k] + d, ly[k], lx[k + 1] + d, ly[k + 1], bolt);
      }
    }
  } else {
    // 非充电：框内显示电量百分比
    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d", lvl);
    int tw = M5Cardputer.Display.textWidth(pct);
    int th = M5Cardputer.Display.fontHeight();
    M5Cardputer.Display.setTextColor((lvl > 20) ? 0x07E0 : 0xF800);   // 低电量红
    M5Cardputer.Display.drawString(pct, cx - tw / 2, by + (bh - th) / 2);
    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);            // 还原主字体
  }
}

void refreshBattery() {                 // 仅局部刷新右上角，不触发整屏重绘
  M5Cardputer.Display.fillRect(202, 0, 38, 18, C_BG);   // 高度 18 -> 不侵入 y>=18 的列表高亮条
  drawBattery();
}

// 在指定区域画六爻卦象: 初爻在底部, 上爻在顶部 (阳=实线, 阴=中间断开)
// hlLine: 需要高亮的爻索引(0=初爻, 5=上爻), -1 = 不高亮
static void drawHexagramFig(int idx, int x0, int w, int yTop, int yBot,
                            uint32_t col, int hlLine = -1) {
  int bits = hexagramLines(HEXAGRAMS[idx].name);
  int step = (yBot - yTop) / 5;
  for (int i = 0; i < 6; i++) {                 // i=0: 初爻(最下)
    int y = yBot - i * step;
    bool hi = (i == hlLine);
    int h  = hi ? 8 : 6;
    int xx = hi ? x0 - 2 : x0;
    int ww = hi ? w + 4 : w;
    uint32_t c = hi ? C_HEX_HL : col;
    if ((bits >> i) & 1) {
      M5Cardputer.Display.fillRect(xx, y, ww, h, c);              // 阳爻: 一条实线
    } else {
      int seg = (ww - 16) / 2;                                    // 阴爻: 两段, 中间留空
      M5Cardputer.Display.fillRect(xx, y, seg, h, c);
      M5Cardputer.Display.fillRect(xx + ww - seg, y, seg, h, c);
    }
  }
}

void drawFooter(const char* txt) {
  M5Cardputer.Display.fillRect(0, FOOTER_Y - 2, 240, FOOTER_H + 2, C_BG);
  M5Cardputer.Display.drawLine(0, FOOTER_Y - 2, 240, FOOTER_Y - 2, C_HINT);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawString(txt, 2, FOOTER_Y + 1);
}

void drawSplash() {
  M5Cardputer.Display.fillScreen(C_BG);
  M5Cardputer.Display.setTextColor(C_TITLE);
  M5Cardputer.Display.drawCentreString("高岛易断", 120, 30);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawCentreString("Takashima Ekidan", 120, 56);
  M5Cardputer.Display.setTextColor(C_TEXT);
  M5Cardputer.Display.drawCentreString("64 卦全文查询", 120, 88);
  drawBattery();
}

void drawList() {
  M5Cardputer.Display.fillScreen(C_BG);
  // 标题
  M5Cardputer.Display.setTextColor(C_TITLE);
  String hdr = "高岛易断 · 选卦";
  if (inputBuf.length()) hdr += "  >" + inputBuf;
  M5Cardputer.Display.drawString(hdr, 2, 0);
  M5Cardputer.Display.setTextColor(C_TEXT);
  for (int r = 0; r < LIST_ROWS; r++) {
    int idx = listTop + r;
    if (idx >= 64) break;
    int y = HEADER_H + r * ROW_H;
    if (idx == sel) {
      M5Cardputer.Display.fillRect(0, y, 156, ROW_H, C_HL);   // 高亮限左侧文字区
      M5Cardputer.Display.setTextColor(C_HL_TXT);
    } else {
      M5Cardputer.Display.setTextColor(C_TEXT);
    }
    char row[24];
    snprintf(row, sizeof(row), "%02d %s", HEXAGRAMS[idx].num, HEXAGRAMS[idx].name);
    M5Cardputer.Display.drawString(row, 4, y);
  }
  // 右侧卦象区
  M5Cardputer.Display.drawLine(SPLIT_X, 20, SPLIT_X, 116, C_GRID);
  drawHexagramFig(sel, 166, 58, 24, 104, C_HEX);
  drawFooter("输号+Enter 上下行 左右页");
  drawBattery();
}

// 详目: 标题行(卦名, 与右上电量框齐平) + 7 个条目 + 右侧卦象
static void drawTocRow(int i) {                 // 局部重绘一行(滚动时用, 避免整屏闪)
  const Hexagram& h = HEXAGRAMS[sel];
  int y = g_tocY0 + i * g_tocRowH;
  bool hl = (i == tocSel);
  M5Cardputer.Display.fillRect(0, y, SPLIT_X - 2, g_tocRowH, hl ? C_HL : C_BG);
  M5Cardputer.Display.setTextColor(hl ? C_HL_TXT : C_TEXT);
  char num[4];
  snprintf(num, sizeof(num), "%d", i + 1);
  M5Cardputer.Display.drawString(num, TOC_NUM_X, y);

  const char* nm = h.secs[i].name;
  if (hl && g_tocSpr) {
    // 选中条目: 整名画进离屏缓冲, 再按像素偏移切一条推上屏 —— 位移是 1px 级的, 不会跳字
    g_tocSpr->fillSprite(C_HL);
    g_tocSpr->setFont(&lgfx::fonts::efontCN_14);
    g_tocSpr->setTextColor(C_HL_TXT);
    g_tocSpr->setTextWrap(false);
    g_tocSpr->setCursor(0, 0);
    g_tocSpr->print(nm);

    uint16_t* src = (uint16_t*)g_tocSpr->getBuffer();
    int sw = g_tocSpr->width();
    for (int r = 0; r < g_tocRowH; r++) {
      memcpy(&g_slice[r * TOC_NAME_W], src + r * sw + g_tsOff, TOC_NAME_W * 2);
    }
    M5Cardputer.Display.pushImage(TOC_NAME_X, y, TOC_NAME_W, g_tocRowH, g_slice);
  } else {
    // 其余条目: 一行放不下就截断 + 省略号, 保持静态(不再一起乱滚)
    M5Cardputer.Display.drawString(truncName(nm, TOC_NAME_W), TOC_NAME_X, y);
  }
}

void drawToc() {
  M5Cardputer.Display.fillScreen(C_BG);

  // 标题行: 只显示本卦卦名, 与右上角电量框同一行(齐平)
  M5Cardputer.Display.setTextColor(C_TITLE);
  M5Cardputer.Display.drawString(HEXAGRAMS[sel].name, 2, 0);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawRightString("Enter开 m返回", 200, 3);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);

  for (int i = 0; i < SEC_COUNT; i++) drawTocRow(i);

  // 右侧: 本卦六爻图, 选中项对应的那一爻高亮(第 1 项"全卦总论"则全亮)
  int figTop = g_tocY0 + 2;
  int figBot = g_tocY0 + SEC_COUNT * g_tocRowH - 8;
  M5Cardputer.Display.drawLine(SPLIT_X, figTop, SPLIT_X, figBot + 6, C_GRID);
  drawHexagramFig(sel, 172, 56, figTop, figBot, C_HEX,
                  tocSel == 0 ? -1 : tocSel - 1);

  drawBattery();
}

void drawDetail() {
  M5Cardputer.Display.fillScreen(C_BG);
  const Hexagram& h = HEXAGRAMS[sel];
  M5Cardputer.Display.setTextColor(C_TITLE);
  char title[40];
  snprintf(title, sizeof(title), "%02d %s · %s", h.num, h.name, h.secs[secIdx].tab);
  M5Cardputer.Display.drawString(title, 2, 0);
  M5Cardputer.Display.setTextColor(C_TEXT);
  int maxTop = (int)detailLines.size() - DETAIL_ROWS;
  if (maxTop < 0) maxTop = 0;
  if (detailTop > maxTop) detailTop = maxTop;
  if (detailTop < 0) detailTop = 0;
  for (int r = 0; r < DETAIL_ROWS; r++) {
    int li = detailTop + r;
    if (li >= (int)detailLines.size()) break;
    M5Cardputer.Display.drawString(detailLines[li], 2, HEADER_H + r * ROW_H);
  }
  char foot[48];
  snprintf(foot, sizeof(foot), "%d/%d 上下行 左右页 m返回",
           detailTop + 1, (int)detailLines.size());
  drawFooter(foot);
  drawBattery();
}

void redraw() {
  switch (view) {
    case SPLASH: drawSplash(); break;
    case LIST:   drawList();   break;
    case TOC:    drawToc();    break;
    default:     drawDetail(); break;
  }
  dirty = false;
}

// ---------- 按键处理 ----------
void handleKeys() {
  auto ks = M5Cardputer.Keyboard.keysState();
  for (char c : ks.word) {
    if (c >= '0' && c <= '9') {
      if (view == LIST && inputBuf.length() < 2) inputBuf += c;   // 最多两位
    } else if (c == 'n' || c == 'N') {
      lineDown();
    } else if (c == 'p' || c == 'P') {
      lineUp();
    } else if (c == 'm' || c == 'M') {
      if (view == DETAIL)      { view = TOC;  dirty = true; }     // 正文 -> 详目
      else if (view == TOC)    { view = LIST; inputBuf = ""; dirty = true; }
    }
    // 方向键(键盘丝印箭头):  ; = 上   . = 下   , = 左   / = 右
    else if (c == ';') { lineUp();   }              // 上: 一行
    else if (c == '.') { lineDown(); }              // 下: 一行
    else if (c == ',') { pageUp();   }              // 左: 一页 / 上一卦
    else if (c == '/') { pageDown(); }              // 右: 一页 / 下一卦
  }
  if (ks.enter) {
    if (view == LIST) {
      if (inputBuf.length() > 0) {
        int v = inputBuf.toInt();
        inputBuf = "";
        if (v >= 1 && v <= 64) openToc(v - 1);
      } else {
        openToc(sel);
      }
    } else if (view == TOC) {
      openSection(tocSel);
    }
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setColorDepth(16);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);
  M5Cardputer.Display.setTextSize(1);

  // 详目布局: 第 0~17 行留给标题(与右上电量框齐平), 7 项铺满余下高度
  int fh = M5Cardputer.Display.fontHeight();
  if (fh < 12) fh = 12;
  const int TOC_TOP = 18;
  int avail = 135 - TOC_TOP;
  if (SEC_COUNT * fh > avail) fh = avail / SEC_COUNT;   // 保证 7 行都放得下
  g_tocRowH = fh;
  g_tocY0   = TOC_TOP + (avail - SEC_COUNT * fh) / 2;

  // 详目滚动用的单行离屏缓冲 —— 有了它, 位移才能做到 1px 级(不跳字)。
  // 分配失败就退化成"截断显示", 不影响其它功能。
  g_tocSpr = new LGFX_Sprite(&M5Cardputer.Display);
  if (g_tocSpr) {
    g_tocSpr->setColorDepth(16);
    g_tocSpr->createSprite(240, g_tocRowH);
    if (!g_tocSpr->getBuffer()) {
      delete g_tocSpr;
      g_tocSpr = nullptr;
    }
  }
  tocScrollReset();

  M5Cardputer.Display.fillScreen(C_BG);
  splashUntil = millis() + 1200;
  view = SPLASH;
  dirty = true;
}

void loop() {
  M5Cardputer.update();
  if (view == SPLASH) {
    if (dirty) redraw();
    if (millis() >= splashUntil) { view = LIST; dirty = true; }
    delay(30);
    return;
  }
  if (M5Cardputer.Keyboard.isChange()) {
    handleKeys();
  }
  if (dirty) redraw();

  // 详目: 只让当前选中那条的名称缓慢平滑滚动(只重绘那一行, 不整屏刷)
  if (view == TOC) tocScrollTick();

  static unsigned long battT = 0;
  if (millis() - battT > 2000) { battT = millis(); refreshBattery(); }
  delay(20);
}
