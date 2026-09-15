/*
 * 高岛易断 · Cardputer 查询器
 * ------------------------------------------------
 * 开机显示 64 卦编号 + 卦名（可上下滚动 / 直接输编号）。
 * 输入编号并按 Enter，显示该卦《高岛易断》详细解读（可滚动）。
 * 中文由 M5GFX 内置 efont 简体中文点阵字库渲染，无需外接字库。
 *
 * 操作：
 *   列表态： 数字键输入编号 -> Enter 打开；  Enter 打开当前选中
 *           上下键(; .)=移动一行    左右键(, /)=翻一整页
 *   详情态： 上下键(; .)=滚动一行  左右键(, /)=翻一整页；  m 返回列表
 *   (n/p 同"上下键"，保留兼容；方向键为键盘丝印箭头的第二功能键)
 *
 * 编译: pio run -d D:/cardputer/gddy
 * 烧录: pio run -d D:/cardputer/gddy -t upload
 */
#include <M5Cardputer.h>
#include <lgfx/Fonts/efont/lgfx_efont_cn.h>
#include <vector>
#include <cstring>
#include "gddy_data.h"

// ---------- UI 常量 ----------
static const int FONT_PX       = 14;    // efontCN_14 每字约 14px
static const int ROW_H         = 16;    // 行高(含间隔)
static const int HEADER_H      = 18;    // 顶部标题行高度
static const int FOOTER_H      = 16;    // 底部提示条高度
static const int FOOTER_Y      = 135 - FOOTER_H;  // 底部提示条顶部 y
static const int CHARS_PER_LINE= 16;    // 每行最多全角字数
static const int LIST_ROWS     = 6;     // 列表可见行数
static const int DETAIL_ROWS   = 6;     // 详情可见行数

// 配色
static const uint32_t C_BG     = 0x0000;
static const uint32_t C_TEXT   = 0xD6E0D6;
static const uint32_t C_TITLE  = 0xFFE0;
static const uint32_t C_HL     = 0x05A0;   // 选中行高亮底
static const uint32_t C_HL_TXT = 0x0000;
static const uint32_t C_HINT   = 0x6AC0;

// ---------- 状态 ----------
enum View { SPLASH, LIST, DETAIL };
static View     view     = SPLASH;
static int      sel      = 0;     // 列表当前选中索引 0..63
static int      listTop  = 0;     // 列表首行索引
static int      detailTop= 0;     // 详情首行索引
static String   inputBuf = "";    // 正在输入的编号
static std::vector<String> detailLines;
static unsigned long splashUntil = 0;
static bool      dirty    = true;

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

void openDetail(int idx) {
  if (idx < 0 || idx > 63) return;
  sel = idx;
  detailTop = 0;
  buildLines(HEXAGRAMS[idx].text, detailLines, CHARS_PER_LINE);
  view = DETAIL;
  dirty = true;
}

void clampListTop() {
  if (sel < listTop) listTop = sel;
  if (sel >= listTop + LIST_ROWS) listTop = sel - LIST_ROWS + 1;
  if (listTop < 0) listTop = 0;
}

// ---------- 滚动控制 ----------
void lineUp() {                                   // 上移一行
  if (view == LIST) { if (sel > 0) --sel; clampListTop(); }
  else { if (detailTop > 0) --detailTop; }
  dirty = true;
}
void lineDown() {                                 // 下移一行
  if (view == LIST) { if (sel < 63) ++sel; clampListTop(); }
  else { ++detailTop; }
  dirty = true;
}
void pageUp() {                                   // 翻一整页(向上)
  if (view == LIST) { sel -= LIST_ROWS; if (sel < 0) sel = 0; clampListTop(); }
  else { detailTop -= DETAIL_ROWS; if (detailTop < 0) detailTop = 0; }
  dirty = true;
}
void pageDown() {                                 // 翻一整页(向下)
  if (view == LIST) { sel += LIST_ROWS; if (sel > 63) sel = 63; clampListTop(); }
  else { detailTop += DETAIL_ROWS; }
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
static void drawHexagramFig(int idx, int x0, int w, int yTop, int yBot, uint32_t col) {
  int bits = hexagramLines(HEXAGRAMS[idx].name);
  const int h = 6;
  int step = (yBot - yTop) / 5;
  for (int i = 0; i < 6; i++) {                 // i=0: 初爻(最下)
    int y = yBot - i * step;
    if ((bits >> i) & 1) {
      M5Cardputer.Display.fillRect(x0, y, w, h, col);            // 阳爻: 一条实线
    } else {
      int seg = (w - 16) / 2;                                     // 阴爻: 两段, 中间留空
      M5Cardputer.Display.fillRect(x0, y, seg, h, col);
      M5Cardputer.Display.fillRect(x0 + w - seg, y, seg, h, col);
    }
  }
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
      M5Cardputer.Display.fillRect(0, y, 156, ROW_H, C_HL);   // 高亮限左侧文字区, 不压右侧卦象
      M5Cardputer.Display.setTextColor(C_HL_TXT);
    } else {
      M5Cardputer.Display.setTextColor(C_TEXT);
    }
    char row[24];
    snprintf(row, sizeof(row), "%02d %s", HEXAGRAMS[idx].num, HEXAGRAMS[idx].name);
    M5Cardputer.Display.drawString(row, 4, y);
  }
  // 右侧卦象区: 显示当前选中卦的六爻图形
  M5Cardputer.Display.drawLine(160, 20, 160, 116, 0x31A6);      // 分栏线
  drawHexagramFig(sel, 166, 58, 24, 104, C_TITLE);
  // 底部提示条（独立区域，不与正文重叠）
  M5Cardputer.Display.fillRect(0, FOOTER_Y - 2, 240, FOOTER_H + 2, C_BG);
  M5Cardputer.Display.drawLine(0, FOOTER_Y - 2, 240, FOOTER_Y - 2, C_HINT);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawString("输号+Enter  上下行  左右页", 2, FOOTER_Y + 1);
  drawBattery();
}

void drawDetail() {
  M5Cardputer.Display.fillScreen(C_BG);
  M5Cardputer.Display.setTextColor(C_TITLE);
  char title[24];
  snprintf(title, sizeof(title), "%02d %s", HEXAGRAMS[sel].num, HEXAGRAMS[sel].name);
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
  M5Cardputer.Display.fillRect(0, FOOTER_Y - 2, 240, FOOTER_H + 2, C_BG);
  M5Cardputer.Display.drawLine(0, FOOTER_Y - 2, 240, FOOTER_Y - 2, C_HINT);
  M5Cardputer.Display.setTextColor(C_HINT);
  char foot[64];
  snprintf(foot, sizeof(foot), "%d/%d  上下行左右页  m返回", detailTop + 1,
           (int)detailLines.size());
  M5Cardputer.Display.drawString(foot, 2, FOOTER_Y + 1);
  drawBattery();
}

void redraw() {
  if (view == SPLASH) drawSplash();
  else if (view == LIST) drawList();
  else drawDetail();
  dirty = false;
}

// ---------- 按键处理 ----------
void handleKeys() {
  auto ks = M5Cardputer.Keyboard.keysState();
  for (char c : ks.word) {
    if (c >= '0' && c <= '9') {
      if (view == LIST) {
        if (inputBuf.length() < 2) inputBuf += c;   // 最多两位
      }
    } else if (c == 'n' || c == 'N') {
      lineDown();
    } else if (c == 'p' || c == 'P') {
      lineUp();
    } else if (c == 'm' || c == 'M') {
      if (view == DETAIL) { view = LIST; inputBuf = ""; dirty = true; }
    }
    // 方向键(键盘丝印箭头):  ; = 上   . = 下   , = 左   / = 右
    else if (c == ';') { lineUp();   }              // 上: 一行
    else if (c == '.') { lineDown(); }              // 下: 一行
    else if (c == ',') { pageUp();   }              // 左: 一页
    else if (c == '/') { pageDown(); }              // 右: 一页
  }
  if (ks.enter) {
    if (view == LIST) {
      if (inputBuf.length() > 0) {
        int v = inputBuf.toInt();
        inputBuf = "";
        if (v >= 1 && v <= 64) openDetail(v - 1);
      } else {
        openDetail(sel);
      }
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
  static unsigned long battT = 0;
  if (millis() - battT > 2000) { battT = millis(); refreshBattery(); }
  delay(20);
}
