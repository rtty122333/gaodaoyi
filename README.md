# 高岛易断 · Cardputer ADV 查询器

把《高岛易断》（高岛吞象 / Takashima Ekidan，明治时代公版文本）全文装进 Cardputer ADV，
开机即可按编号查任意一卦的详细占断解读，中文在 1.14" 屏上实时渲染、可滚动。

## 数据来源（最全公开版）
- 站点：`gaodaoyiduan.org/original/` 的 64 个卦页（每卦含卦辞、《彖传》《大象》、六爻逐爻的
  「高岛占断」与「占例」）。
- 抓取：64 页 HTML 已存于 `raw/`，由 `src/gddy_data.h` 脚本化提取（卦名 + 全文，UTF-8）。
- 体量：64 卦正文合计约 **1.12 MB**（平均每卦 18KB），为目前能拿到的较完整版本。

## 中文显示（关键技巧）
- **M5GFX 库自带简体中文 efont 点阵字库**（`lgfx::fonts::efontCN_12/14/16/24`），
  直接 `setFont(&lgfx::fonts::efontCN_12)` 即可显示中文，**无需自己生成/外接字库**。
- 文本以 `const char*`（UTF-8）烧进 Flash，运行时按字数折行、分页滚动。

## 工程文件
- `platformio.ini` —— 用本地 M5Stack 平台（`D:/cardputer/platform-m5stack`）+ `m5stack_cardputer` 板型；
  因数据+字库较大，已用 `partitions.csv`（`max_app_8MB`，app 分区 ~7.9MB）突破默认 1.25MB 上限。
- `src/main.cpp` —— 主程序：开机显示 64 卦编号+卦名（可滚动），输入编号看详解。
- `src/gddy_data.h` —— 自动生成的 64 卦数据（编号 / 卦名 / 全文）。
- `partitions.csv` —— 8MB Flash 最大 app 分区表。
- `raw/` —— 64 卦原始 HTML（留档，可重跑解析脚本更新数据）。

## 操作
- 列表态：数字键输入编号（1–64）→ `Enter` 打开； `n` 下翻 / `p` 上翻；直接 `Enter` 打开当前选中。
- 详情态： `n` 下滚 / `p` 上滚； `m` 返回列表。

## 构建与烧录
```bat
pio run -d D:/cardputer/gddy            # 编译
pio run -d D:/cardputer/gddy -t upload  # 烧录（插线；必要时按住侧面 G0/BOOT 进下载模式）
pio device monitor -b 115200            # 看串口
```
> 若日后 `pio platform update` 把框架升级、编译报 `pins_arduino.h` 缺失，跑一次
> `python D:/cardputer/fix_variant.py` 重注入 Cardputer 板型头文件。

## 在 velxio.dev 预览
velxio 的 Cardputer ADV 用同款 `M5Cardputer` 库，本工程 `src/main.cpp` 可直接粘进
`velxio.dev/editor/`（选 M5 Cardputer ADV）运行；`gddy_data.h` 也一并粘入 `src/` 即可。
注意 velxio 是否内置 efont 中文字库取决于其环境，若缺中文可改回 ASCII 提示或在该平台另行加载字库。

## 备注
- 全本《高岛易断》体量很大（原书数百页），这里收录的是「每卦完整占断」级文本；若想进一步
  精简体积，可在解析脚本里只保留卦辞+六爻断语、去掉占例。
- 想更新/扩充数据：改 `raw/` 下 HTML 或替换来源，重跑 `gddy_data.h` 生成逻辑（见本仓库
  `D:/cardputer/gddy/` 的生成脚本思路）即可。
