/*
 * 高岛易断 · Cardputer 查询器
 * ------------------------------------------------
 * 开机进「主菜单」，两件事：
 *
 *   一、数字起卦占卜 —— 傅佩荣《易经入门 17 讲》的随机数(数字)起卦法，数字由用户自己报
 *       输入 9 位数字 = 三组三位数：
 *         第一组 ÷ 8 取余 = 下卦(内卦)   第二组 ÷ 8 取余 = 上卦(外卦)
 *         第三组 ÷ 6 取余 = 动爻(变爻)
 *       余数对照先天八卦数(乾1 兑2 离3 震4 巽5 坎6 艮7 坤8)，除尽记 8 / 记 6。
 *       动爻阴阳互换即得「之卦」。起卦后 Enter 直接进本卦详目，光标停在动爻那一条上，
 *       b 可切去看之卦 —— 断卦就是「本卦 + 动爻爻辞 + 之卦」这三件套。
 *
 *   二、高岛易断原文 —— 三层浏览：
 *       ① 选卦  64 卦编号+卦名列表，右侧显示该卦六爻图形
 *       ② 详目  选定一卦后列出 7 个条目 ——「全卦总论」+ 初/二/三/四/五/上六爻
 *       ③ 正文  所选条目的详细解读，可滚动
 *
 * 中文由 M5GFX 内置 efont 简体中文点阵字库渲染，无需外接字库。
 *
 * 操作：
 *   主菜单： 上下键(; .)=选项   左右键(, /)=选项    Enter=进入
 *   数字输入：0-9=输入(共 9 位)  del=退格  Enter=起卦  m=回主菜单
 *            9 位里任何一位都可以是 0（含 004 这类，已放开"每组首位非 0"的严格设定）
 *            输入过程中不显示任何换算结果(卦名/余数/爻位)，只报进度 ——
 *            以免报数者边输边看到卦象、影响判断；换算结果统一在起卦结果页揭晓。
 *   起卦态： Enter=看本卦(停在动爻)   b=看之卦   r=回去改数字   m=回主菜单
 *   选卦态： 数字键输编号 -> Enter 打开；  Enter 打开当前选中
 *            上下键=移动一行   左右键=翻一整页   m=回主菜单
 *   详目态： 标题行只显示卦名(与右上电量框同一行)，下列 7 个条目，
 *            右侧卦象高亮当前条目对应的那一爻；
 *            条目名一行放不下时 —— 只有"当前选中"那条缓慢平滑滚动(逐像素,
 *            停-滚-停-回)，其余条目静止截断显示前几字 + 省略号
 *            上下键=选项移动         左右键=上一卦 / 下一卦
 *            Enter=进入正文          m=返回
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
#include "sup_font.h"          // 补字字体(efont 缺的生僻字), 由 _mkfont.py 生成

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
enum View { SPLASH, MODE, LIST, TOC, DETAIL, CAST, INPUT9 };
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

// 主菜单: 数字起卦 / 原文
static const int MODE_ITEMS = 2;
static int       modeSel    = 0;      // 0=数字起卦占卜  1=高岛易断原文

// 数字起卦: 用户自己报的 9 位数字(三组三位数, 顺序 = 下卦 / 上卦 / 动爻)
static const int DIGITS_N = 9;
static String    digits   = "";
static String    g_hint   = "";       // 输入页临时提示(如"每组首位不能为0")
static unsigned long g_hintUntil = 0;
static View      tocBack  = LIST;     // 详目按 m 返回到哪个视图

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

// ---------- 数字卦（傅佩荣《易经入门17讲》随机数起卦法）----------
// 用户自己报 9 位数字 = 三组三位数（本实现允许任一位为 0, 见 inputAppend 的说明）：
//   第一组 ÷ 8 取余 -> 下卦（内卦）      除尽记 8 -> 坤
//   第二组 ÷ 8 取余 -> 上卦（外卦）      除尽记 8 -> 坤
//   第三组 ÷ 6 取余 -> 动爻（变爻）      除尽记 6 -> 上爻
// 余数对照先天八卦数：乾1 兑2 离3 震4 巽5 坎6 艮7 坤8
// 注意顺序：第一组是【下卦】，第二组才是【上卦】，与常见的报数法相反。
static const int XT_BITS[9] = { 0, 7, 3, 5, 1, 6, 2, 4, 0 };   // 先天数 1..8 -> 三爻bit

static const char* GUA8[9]   = { "", "乾", "兑", "离", "震", "巽", "坎", "艮", "坤" };
static const char* XIANG8[9] = { "", "天", "泽", "火", "雷", "风", "水", "山", "地" };
static const char* YAO6[7]   = { "", "初", "二", "三", "四", "五", "上" };

struct CastResult {
  int  n[3];        // 三组三位数
  int  r[3];        // 三组余数
  int  idx;         // 本卦索引
  int  dong;        // 动爻 1..6 (1=初爻)
  int  bianIdx;     // 之卦索引
  bool valid;
};
static CastResult g_cast = { {0, 0, 0}, {0, 0, 0}, -1, 1, -1, false };

// 由六爻 bits 反查卦在本表的索引（表按周易卦序排列，只能查）
static int guaIndexFromBits(int bits) {
  for (int i = 0; i < 64; i++) {
    if (hexagramLines(HEXAGRAMS[i].name) == bits) return i;
  }
  return -1;
}

// 取第 g 组的三位数
static int groupNum3(const String& d, int g) {
  return (d[g * 3] - '0') * 100 + (d[g * 3 + 1] - '0') * 10 + (d[g * 3 + 2] - '0');
}

// 纯函数: 由 9 位数字算出本卦 / 动爻 / 之卦（不改变全局状态, 供预览与起卦共用）
static CastResult calcCast(const String& d) {
  CastResult c = { {0, 0, 0}, {0, 0, 0}, -1, 1, -1, false };
  if ((int)d.length() != DIGITS_N) return c;
  for (int i = 0; i < 3; i++) {
    c.n[i] = groupNum3(d, i);
    int m  = (i < 2) ? 8 : 6;
    int r  = c.n[i] % m;
    c.r[i] = (r == 0) ? m : r;                       // 除尽记 8 / 记 6
  }
  int bits  = XT_BITS[c.r[0]] | (XT_BITS[c.r[1]] << 3);   // 第一组=下卦, 第二组=上卦
  c.idx     = guaIndexFromBits(bits);
  c.dong    = c.r[2];                                    // 1..6, 1=初爻
  c.bianIdx = (c.idx >= 0) ? guaIndexFromBits(bits ^ (1 << (c.dong - 1))) : -1;
  c.valid   = (c.idx >= 0 && c.bianIdx >= 0);
  return c;
}

// 输入页: 追加一位数字
// 注: 9 位里任何一位都允许是 0（含 004 这类）—— 傅佩荣原设定要求"三组三位数"(首位非 0,
//     理由是数字太小太好心算、会削弱随意性), 这里按使用习惯放开了该限制。
//     代价: 某一组数值 <= 7(÷8) 或 <= 5(÷6) 时余数等于该数本身, 取余不再起遮蔽作用。
//     若要恢复严格模式, 在此处加回 `if (digits.length() % 3 == 0 && c == '0') return;` 即可。
static void inputAppend(char c) {
  if ((int)digits.length() >= DIGITS_N) return;
  digits += c;
  g_hint  = "";            // 有新输入就撤掉上一次的提示
  dirty   = true;
}

static void inputDel() {
  if (digits.length() > 0) digits.remove(digits.length() - 1);
  g_hint = "";
  dirty  = true;
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
void openToc(int idx, int secSel = 0, View back = LIST) {  // 进「详目」(secSel: 光标停哪条)
  if (idx < 0 || idx > 63) return;
  sel = idx;
  tocSel = secSel;
  if (tocSel < 0) tocSel = 0;
  if (tocSel > SEC_COUNT - 1) tocSel = SEC_COUNT - 1;
  listTop = 0;
  tocBack = back;
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
// 起卦页 / 输入页没有"行/页"可翻, 方向键在此无效
void lineUp() {                                   // 上移一行 / 上移一项
  if (view == MODE)      { if (modeSel > 0) { --modeSel; dirty = true; } return; }
  if (view == CAST || view == INPUT9) return;
  if (view == LIST)      { if (sel > 0) --sel; clampListTop(); }
  else if (view == TOC)  { if (tocSel > 0) { --tocSel; tocScrollReset(); } }
  else                   { if (detailTop > 0) --detailTop; }
  dirty = true;
}
void lineDown() {                                 // 下移一行 / 下移一项
  if (view == MODE)      { if (modeSel < MODE_ITEMS - 1) { ++modeSel; dirty = true; } return; }
  if (view == CAST || view == INPUT9) return;
  if (view == LIST)      { if (sel < 63) ++sel; clampListTop(); }
  else if (view == TOC)  { if (tocSel < SEC_COUNT - 1) { ++tocSel; tocScrollReset(); } }
  else                   { ++detailTop; }
  dirty = true;
}
void pageUp() {                                   // 左键
  if (view == MODE)      { modeSel = (modeSel + MODE_ITEMS - 1) % MODE_ITEMS; dirty = true; return; }
  if (view == CAST || view == INPUT9) return;
  if (view == LIST)      { sel -= LIST_ROWS; if (sel < 0) sel = 0; clampListTop(); }
  else if (view == TOC)  { if (sel > 0) { --sel; tocSel = 0; tocScrollReset(); } }  // 上一卦
  else                   { detailTop -= DETAIL_ROWS; if (detailTop < 0) detailTop = 0; }
  dirty = true;
}
void pageDown() {                                 // 右键
  if (view == MODE)      { modeSel = (modeSel + 1) % MODE_ITEMS; dirty = true; return; }
  if (view == CAST || view == INPUT9) return;
  if (view == LIST)      { sel += LIST_ROWS; if (sel > 63) sel = 63; clampListTop(); }
  else if (view == TOC)  { if (sel < 63) { ++sel; tocSel = 0; tocScrollReset(); } } // 下一卦
  else                   { detailTop += DETAIL_ROWS; }
  dirty = true;
}

// ---------- 补字字体: efont 缺字的兜底 ----------
// M5GFX 自带的 efont 简体字库只收 GB2312 那一档(约 7500 字)。《高岛易断》正文里有 160 多个
// 生僻字(姤 / 夬 / 禴 / 繘…), efont 查不到, U8g2font::drawChar 就调 drawCharDummy 画一个
// 方块 —— 屏幕上那些"缺字方块 □"就是它, 不是数据坏了。
//
// sup_font.h 由 _mkfont.py 从系统宋体 12px/14px 点阵生成, 两档字号与 efontCN_12/_14 一一对应。
// 下面这套小工具在遇到缺字时逐字切到补字字体, 其它字仍由 efont 原样绘制。
//
// 关键: 整串都不含缺字时, drawTextOn 直接走原来的 drawString / drawCentreString /
//       drawRightString —— 渲染结果与改动前逐像素一致, 既有排版完全不受影响;
//       只有真含缺字的串才走逐段混排。补字字形的度量(dx/max_h/y_offset)与 efont 对齐,
//       所以换字体不会让字距或行距跳动。
static const lgfx::U8g2font g_supFont14(supFontData14);
static const lgfx::U8g2font g_supFont12(supFontData12);

// 取与当前字体同字号的补字字体, 并交出它的覆盖码表(升序)
static const lgfx::IFont* supMatch(const lgfx::IFont* cur, const uint16_t** cps, int* cnt) {
  if (cur == (const lgfx::IFont*)&lgfx::fonts::efontCN_12) {
    *cps = SUP_CPS_12; *cnt = SUP_COUNT_12;
    return &g_supFont12;
  }
  *cps = SUP_CPS_14; *cnt = SUP_COUNT_14;
  return &g_supFont14;
}

static bool supCovered(const uint16_t* t, int n, uint16_t cp) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if      (t[mid] == cp) return true;
    else if (t[mid] <  cp) lo = mid + 1;
    else                   hi = mid - 1;
  }
  return false;
}

// 该码点要不要交给补字字体? 要则返回交给它的码点(非 BMP 换成私用区), 不要则返回 0
static uint32_t supPick(uint32_t cp, const uint16_t* t, int n) {
  if (cp < 0x80) return 0;
  uint32_t rc = cp;
  for (int i = 0; i < SUP_NONBMP_COUNT; i++) {
    if (SUP_NONBMP[i][0] == cp) { rc = SUP_NONBMP[i][1]; break; }
  }
  if (rc > 0xFFFF) return 0;
  return supCovered(t, n, (uint16_t)rc) ? rc : 0;
}

static uint32_t utf8Next(const char*& p) {          // 解一个码点, p 前进
  unsigned char c = (unsigned char)*p;
  int len = 1;
  uint32_t cp = c;
  if      ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
  for (int i = 1; i < len; i++) cp = (cp << 6) | ((unsigned char)p[i] & 0x3F);
  p += len;
  return cp;
}

static int utf8Put(char* b, uint32_t cp) {          // 编码一个码点, 返回字节数
  if (cp < 0x800) {
    if (cp < 0x80) { b[0] = (char)cp; return 1; }
    b[0] = (char)(0xC0 | (cp >> 6));  b[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    b[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  b[0] = (char)(0xF0 | (cp >> 18));   b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

// 逐字混排绘制。al: 0=左(x 为左边界) 1=居中(x 为中心) 2=右(x 为右边界)
// 全串无缺字 -> 交给 M5GFX 原路径; 否则把串切成"同一字体"的连续段, 逐段 drawString。
// 段宽用 textWidth(段的独立 C 串, 该段的字体) 量, 与 M5GFX 内部排版同一套算法。
template <typename G>
static void drawTextOn(G& g, const char* s, int32_t x, int32_t y, int al) {
  if (!s || !*s) return;
  const lgfx::IFont* main = g.getFont();
  const uint16_t* tps;  int nt;
  const lgfx::IFont* sup = supMatch(main, &tps, &nt);

  bool need = false;                              // 先扫一遍, 有没有缺字
  for (const char* p = s; *p; ) {
    const char* q = p;
    if (supPick(utf8Next(q), tps, nt)) { need = true; break; }
    p = q;
  }
  if (!need) {
    if      (al == 1) g.drawCentreString(s, x, y);
    else if (al == 2) g.drawRightString (s, x, y);
    else              g.drawString      (s, x, y);
    return;
  }

  // 有缺字: 重编码进 buf(非 BMP 换成私用区码点), 并按"用哪个字体"切段
  struct Seg { int off, len, w; const lgfx::IFont* f; };
  static char buf[224];
  static Seg  seg[64];
  int nseg = 0, bl = 0;
  for (const char* p = s; *p && bl <= (int)sizeof(buf) - 5; ) {
    const char* q = p;
    uint32_t cp = utf8Next(q);
    uint32_t rc = supPick(cp, tps, nt);
    const lgfx::IFont* f = rc ? sup : main;
    int w = utf8Put(buf + bl, rc ? rc : cp);
    if (nseg == 0 || seg[nseg - 1].f != f) {
      if (nseg >= (int)(sizeof(seg) / sizeof(seg[0]))) break;
      seg[nseg].off = bl; seg[nseg].len = w; seg[nseg].w = 0; seg[nseg].f = f;
      ++nseg;
    } else {
      seg[nseg - 1].len += w;
    }
    bl += w;
    p = q;
  }
  if (nseg == 0) return;
  buf[bl] = 0;

  int32_t total = 0;
  for (int k = 0; k < nseg; k++) {
    char* sp = buf + seg[k].off;
    char  sv = sp[seg[k].len];
    sp[seg[k].len] = 0;                           // 临时截断成独立 C 串来量宽
    seg[k].w = g.textWidth(sp, seg[k].f);
    sp[seg[k].len] = sv;
    total += seg[k].w;
  }

  int32_t sx = x;
  if      (al == 1) sx = x - total / 2;
  else if (al == 2) sx = x - total;

  // 逐段绘制。注意: 各段在 buf 里是紧挨着的, 只有整串末尾有 NUL —— 所以每段绘制前
  // 必须临时把"本段末字节"改写成 0 截断, 画完再还原。否则 drawString 会从本段起点
  // 一直画到整串结尾, 同一段文字被反复重画: 用 efont 画的那遍把生僻字画成方块,
  // 用补字字体画的那遍又把常用字画成方块, 叠在一起就是"字隐约可见 + 一堆方框重叠"。
  for (int k = 0; k < nseg; k++) {
    char* sp = buf + seg[k].off;
    char  sv = sp[seg[k].len];                    // 原本是下一段的首字节(末段则是整串 NUL)
    sp[seg[k].len] = 0;
    g.setFont(seg[k].f);
    g.drawString(sp, sx, y);
    sp[seg[k].len] = sv;
    sx += seg[k].w;
  }
  g.setFont(main);                                // 还原, 免得影响后续绘制
}

// 画到主屏(左对齐)
static void drawText(const char* s, int32_t x, int32_t y, int al = 0) {
  drawTextOn(M5Cardputer.Display, s, x, y, al);
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

void drawFooter(const char* txt, int small = 0) {
  M5Cardputer.Display.fillRect(0, FOOTER_Y - 2, 240, FOOTER_H + 2, C_BG);
  M5Cardputer.Display.drawLine(0, FOOTER_Y - 2, 240, FOOTER_Y - 2, C_HINT);
  M5Cardputer.Display.setTextColor(C_HINT);
  if (small) {                                  // 窄栏用 12px, 好塞下更长的提示
    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
    M5Cardputer.Display.drawString(txt, 2, FOOTER_Y + 3);
    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);
  } else {
    M5Cardputer.Display.drawString(txt, 2, FOOTER_Y + 1);
  }
}

void drawSplash() {
  M5Cardputer.Display.fillScreen(C_BG);
  M5Cardputer.Display.setTextColor(C_TITLE);
  M5Cardputer.Display.drawCentreString("高岛易断", 120, 30);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawCentreString("Takashima Ekidan", 120, 56);
  M5Cardputer.Display.setTextColor(C_TEXT);
  M5Cardputer.Display.drawCentreString("数字起卦 · 64 卦全文", 120, 88);
  drawBattery();
}

// 主菜单: 1 数字起卦占卜 / 2 高岛易断原文
void drawMode() {
  M5Cardputer.Display.fillScreen(C_BG);
  M5Cardputer.Display.setTextColor(C_TITLE);
  M5Cardputer.Display.drawString("高岛易断", 2, 0);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawRightString("上下选 Enter确定", 200, 3);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);

  const char* items[MODE_ITEMS] = { "数字起卦占卜", "高岛易断原文" };
  for (int i = 0; i < MODE_ITEMS; i++) {
    int  y  = 44 + i * 28;
    bool hl = (i == modeSel);
    if (hl) {
      M5Cardputer.Display.fillRect(0, y - 4, 240, 26, C_HL);
      M5Cardputer.Display.setTextColor(C_HL_TXT);
    } else {
      M5Cardputer.Display.setTextColor(C_TEXT);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d  %s", i + 1, items[i]);
    M5Cardputer.Display.drawString(buf, 26, y);
  }

  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  M5Cardputer.Display.setTextColor(C_HINT);
  if (modeSel == 0) {
    M5Cardputer.Display.drawString("傅佩荣随机数起卦法", 8, 100);
    M5Cardputer.Display.drawString("报 9 位数字 → 本卦·动爻·之卦", 8, 115);
  } else {
    M5Cardputer.Display.drawString("《高岛易断》64 卦全文", 8, 100);
    M5Cardputer.Display.drawString("查卦辞 / 六爻占断", 8, 115);
  }
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);

  drawBattery();
}

// 数字起卦: 输入 9 位数字(三组三位数 -> 下卦 / 上卦 / 动爻)
// 输入过程中**不显示**任何换算结果(卦名 / 余数 / 爻位) —— 边输边看到卦象会给报数者心理暗示,
// 破坏"随意报数"的初衷, 所以这里只报进度; 换算结果统一等到起卦结果页才揭晓。
void drawInput() {
  M5Cardputer.Display.fillScreen(C_BG);
  M5Cardputer.Display.setTextColor(C_TITLE);
  M5Cardputer.Display.drawString("数字起卦", 2, 0);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawRightString("三组三位数", 200, 3);

  const char* lab[3] = { "下卦", "上卦", "动爻" };
  const int   bx0 = 31, bw = 42, bh = 28, by = 38, step = 70;
  int         len = (int)digits.length();

  char buf[40];
  for (int g = 0; g < 3; g++) {
    int  bx     = bx0 + g * step;
    bool active = (len / 3 == g) && (len < DIGITS_N);     // 正在输入的那一组
    M5Cardputer.Display.drawRoundRect(bx, by, bw, bh, 3, active ? C_TITLE : C_GRID);

    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);
    for (int k = 0; k < 3; k++) {                          // 三位数字, 每位 7px, 居中
      int gi = g * 3 + k;
      int x  = bx + (bw - 21) / 2 + k * 7;
      if (gi < len) {
        M5Cardputer.Display.setTextColor(active ? C_TITLE : C_TEXT);
        char s[2] = { digits[gi], 0 };
        M5Cardputer.Display.drawString(s, x, by + 6);
      } else {                                             // 未输入: 短下划线
        M5Cardputer.Display.fillRect(x + 1, by + bh - 9, 5, 1, C_GRID);
      }
    }

    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
    M5Cardputer.Display.setTextColor(active ? C_TITLE : C_HINT);
    M5Cardputer.Display.drawCentreString(lab[g], bx + bw / 2, by + bh + 4);
  }

  // 只报进度, 不透任何卦象/爻位信息
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  if (g_hint.length()) {
    M5Cardputer.Display.setTextColor(C_HEX_HL);
    M5Cardputer.Display.drawCentreString(g_hint, 120, 100);
  } else if (len >= DIGITS_N) {
    M5Cardputer.Display.setTextColor(C_TITLE);
    M5Cardputer.Display.drawCentreString("输满了, 按 Enter 起卦", 120, 100);
  } else {
    M5Cardputer.Display.setTextColor(C_GRID);
    snprintf(buf, sizeof(buf), "还需输入 %d 位", DIGITS_N - len);
    M5Cardputer.Display.drawCentreString(buf, 120, 100);
  }

  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);
  drawFooter("Enter起卦 del退格 m返回", 1);
  drawBattery();
}

void drawList() {
  M5Cardputer.Display.fillScreen(C_BG);
  // 标题
  M5Cardputer.Display.setTextColor(C_TITLE);
  String hdr = "高岛易断 · 选卦";
  if (inputBuf.length()) hdr += "  >" + inputBuf;
  drawText(hdr.c_str(), 2, 0);
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
    drawText(row, 4, y);
  }
  // 右侧卦象区
  M5Cardputer.Display.drawLine(SPLIT_X, 20, SPLIT_X, 116, C_GRID);
  drawHexagramFig(sel, 166, 58, 24, 104, C_HEX);
  drawFooter("输号 Enter选 上下行 m返回", 1);
  drawBattery();
}

// 起卦结果页: 用户报的三组数 -> 下卦/上卦/动爻 -> 本卦, 右侧画本卦六爻(动爻高亮)
void drawCast() {
  M5Cardputer.Display.fillScreen(C_BG);
  M5Cardputer.Display.setTextColor(C_TITLE);
  M5Cardputer.Display.drawString("起卦结果", 2, 0);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  M5Cardputer.Display.setTextColor(C_HINT);
  M5Cardputer.Display.drawRightString("傅佩荣数字卦", 200, 3);
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);

  if (!g_cast.valid) {                       // 兜底: 正常不会走到
    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
    M5Cardputer.Display.setTextColor(C_HINT);
    M5Cardputer.Display.drawString("换算异常, 按 r 重新输入", 2, 40);
    M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);
    drawFooter("r改数字 m返回", 1);
    drawBattery();
    return;
  }

  char buf[64];
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  // 三组数: 第一组定下卦, 第二组定上卦, 第三组定动爻
  for (int i = 0; i < 3; i++) {
    int y = 20 + i * 15;
    if (i < 2) {
      snprintf(buf, sizeof(buf), "%s卦 %d/8 余%d %s(%s)",
               (i == 0) ? "下" : "上", g_cast.n[i], g_cast.r[i],
               GUA8[g_cast.r[i]], XIANG8[g_cast.r[i]]);
    } else {
      snprintf(buf, sizeof(buf), "动爻 %d/6 余%d %s爻",
               g_cast.n[i], g_cast.r[i], YAO6[g_cast.r[i]]);
    }
    M5Cardputer.Display.setTextColor(C_HINT);
    M5Cardputer.Display.drawString(buf, 2, y);
  }
  M5Cardputer.Display.drawLine(0, 66, 168, 66, C_GRID);

  // 本卦
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);
  M5Cardputer.Display.setTextColor(C_TITLE);
  snprintf(buf, sizeof(buf), "%s 第%d卦",
           HEXAGRAMS[g_cast.idx].name, HEXAGRAMS[g_cast.idx].num);
  drawText(buf, 2, 70);

  // 动爻与之卦
  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_12);
  M5Cardputer.Display.setTextColor(C_TEXT);
  snprintf(buf, sizeof(buf), "%s爻动 -> 之卦 %s", YAO6[g_cast.dong],
           (g_cast.bianIdx >= 0) ? HEXAGRAMS[g_cast.bianIdx].name : "?");
  drawText(buf, 2, 92);

  M5Cardputer.Display.setTextColor(C_GRID);
  M5Cardputer.Display.drawString("断卦: 本卦+动爻+之卦", 2, 104);

  M5Cardputer.Display.setFont(&lgfx::fonts::efontCN_14);

  // 右侧: 本卦六爻图, 动爻红色高亮
  drawHexagramFig(g_cast.idx, 178, 48, 24, 100, C_HEX, g_cast.dong - 1);

  drawFooter("Enter本卦 b之卦 r改数 m返回", 1);
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
    drawTextOn(*g_tocSpr, nm, 0, 0, 0);

    uint16_t* src = (uint16_t*)g_tocSpr->getBuffer();
    int sw = g_tocSpr->width();
    for (int r = 0; r < g_tocRowH; r++) {
      memcpy(&g_slice[r * TOC_NAME_W], src + r * sw + g_tsOff, TOC_NAME_W * 2);
    }
    M5Cardputer.Display.pushImage(TOC_NAME_X, y, TOC_NAME_W, g_tocRowH, g_slice);
  } else {
    // 其余条目: 一行放不下就截断 + 省略号, 保持静态(不再一起乱滚)
    drawText(truncName(nm, TOC_NAME_W).c_str(), TOC_NAME_X, y);
  }
}

void drawToc() {
  M5Cardputer.Display.fillScreen(C_BG);

  // 标题行: 只显示本卦卦名, 与右上角电量框同一行(齐平)
  M5Cardputer.Display.setTextColor(C_TITLE);
  drawText(HEXAGRAMS[sel].name, 2, 0);
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
  drawText(title, 2, 0);
  M5Cardputer.Display.setTextColor(C_TEXT);
  int maxTop = (int)detailLines.size() - DETAIL_ROWS;
  if (maxTop < 0) maxTop = 0;
  if (detailTop > maxTop) detailTop = maxTop;
  if (detailTop < 0) detailTop = 0;
  for (int r = 0; r < DETAIL_ROWS; r++) {
    int li = detailTop + r;
    if (li >= (int)detailLines.size()) break;
    drawText(detailLines[li].c_str(), 2, HEADER_H + r * ROW_H);
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
    case MODE:   drawMode();   break;
    case LIST:   drawList();   break;
    case CAST:   drawCast();   break;
    case INPUT9:  drawInput();  break;
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
      if (view == LIST)       { if (inputBuf.length() < 2) inputBuf += c; }   // 最多两位
      else if (view == INPUT9) { inputAppend(c); }
    } else if (c == 'n' || c == 'N') {
      lineDown();
    } else if (c == 'p' || c == 'P') {
      lineUp();
    } else if (c == 'r' || c == 'R') {
      if (view == CAST) { view = INPUT9; dirty = true; }          // 回去改数字
    } else if (c == 'b' || c == 'B') {
      if (view == CAST && g_cast.valid && g_cast.bianIdx >= 0) {  // 直接看之卦
        openToc(g_cast.bianIdx, g_cast.dong, CAST);
      }
    } else if (c == 'm' || c == 'M') {
      if (view == DETAIL)      { view = TOC;  dirty = true; }     // 正文 -> 详目
      else if (view == TOC)    { view = tocBack; inputBuf = ""; dirty = true; }
      else if (view == LIST)   { view = MODE; inputBuf = ""; dirty = true; }
      else if (view == CAST)   { view = MODE; dirty = true; }
      else if (view == INPUT9)  { view = MODE; g_hint = ""; dirty = true; }
    }
    // 方向键(键盘丝印箭头):  ; = 上   . = 下   , = 左   / = 右
    else if (c == ';') { lineUp();   }              // 上: 一行
    else if (c == '.') { lineDown(); }              // 下: 一行
    else if (c == ',') { pageUp();   }              // 左: 一页 / 上一卦
    else if (c == '/') { pageDown(); }              // 右: 一页 / 下一卦
  }

  if (view == INPUT9 && ks.del) inputDel();          // 退格(backspace 不进 word)

  if (ks.enter) {
    if (view == MODE) {
      if (modeSel == 0) {                    // 数字起卦: 从零开始报数
        view = INPUT9;
        digits = "";
        g_hint = "";
        dirty = true;
      } else              { view = LIST;  dirty = true; }   // 原文
    } else if (view == INPUT9) {
      if ((int)digits.length() < DIGITS_N) {
        g_hint      = "需输满 9 位数字";
        g_hintUntil = millis() + 1500;
        dirty       = true;
      } else {
        g_cast = calcCast(digits);
        if (g_cast.valid) { view = CAST; g_hint = ""; dirty = true; }
        else { g_hint = "换算失败, 请重输"; g_hintUntil = millis() + 1500; dirty = true; }
      }
    } else if (view == LIST) {
      if (inputBuf.length() > 0) {
        int v = inputBuf.toInt();
        inputBuf = "";
        if (v >= 1 && v <= 64) openToc(v - 1, 0, LIST);
      } else {
        openToc(sel, 0, LIST);
      }
    } else if (view == TOC) {
      openSection(tocSel);
    } else if (view == CAST) {
      // 直接进本卦详目, 光标停在动爻那一条上 —— 断卦就看它
      if (g_cast.valid) openToc(g_cast.idx, g_cast.dong, CAST);
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
    if (millis() >= splashUntil) { view = MODE; dirty = true; }   // 开机 -> 主菜单
    delay(30);
    return;
  }
  if (M5Cardputer.Keyboard.isChange()) {
    handleKeys();
  }
  // 输入页的临时提示到时自动消失
  if (g_hint.length() && millis() > g_hintUntil) {
    g_hint = "";
    if (view == INPUT9) dirty = true;
  }
  if (dirty) redraw();

  // 详目: 只让当前选中那条的名称缓慢平滑滚动(只重绘那一行, 不整屏刷)
  if (view == TOC) tocScrollTick();

  static unsigned long battT = 0;
  if (millis() - battT > 2000) { battT = millis(); refreshBattery(); }
  delay(20);
}
