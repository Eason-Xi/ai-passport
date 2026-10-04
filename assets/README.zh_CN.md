<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 颜真卿字帖（多宝塔碑）字体

| 文件 | 字号 / bpp | 字符 | 用途 |
| --- | --- | --- | --- |
| [`fonts/yz_zh14.c`](fonts/yz_zh14.c) | 14 px / 4 bpp | `main/yz_strings.h` 全部字符串字面量 + `tools/yz_catalog.json` 中显示的全部文字（帖名、年代、书者、简介、拓本来源、卷名，以及每个字的简体、繁体、拼音、碑文语境、出处）+ 可打印 ASCII，约 1420 字 | 正文、提示、页签、笔法卡、电量 |
| [`fonts/yz_zh18.c`](fonts/yz_zh18.c) | 18 px / 4 bpp | 同 `yz_zh14` | 菜单、标题行、卷名、拼音 |
| [`fonts/yz_zh24.c`](fonts/yz_zh24.c) | 24 px / 4 bpp | 同 `yz_zh14` | 目录字格（碑上原字的通行繁体）、页面标题、计时 |
| [`fonts/yz_zh32.c`](fonts/yz_zh32.c) | 32 px / 4 bpp | 帖名、字目全部简体单字、数字（849 字） | 竖排帖名、简体大字 |

- 来源：Adobe Source Han Sans SC 2.005（`SourceHanSansSC-Regular.otf`，SubsetOTF/SC），SIL Open Font License 1.1。OTF 不提交，其 SHA-256 记录在 [`fonts/yz_fonts.manifest.json`](fonts/yz_fonts.manifest.json)。
- 转换器：`lv_font_conv` 1.5.3，参数 `--no-compress --no-kerning`，LVGL 9.5 格式；manifest 记录每个字体的完整命令、码点范围与输出哈希。
- 修改界面文字或字目后重新生成：`python3 tools/gen_yz_fonts.py generate --lv-font-conv <lv_font_conv> --font <SourceHanSansSC-Regular.otf>`（需要 fontTools）。生成器同时重写 `main/yz_font_glyphs.h`，固件启动时用它逐码点自检。
- `python3 tools/gen_yz_fonts.py check`（已纳入 `tools/validate.sh --static`）只用标准库：解析已提交字体的 cmap，缺字、文件过期、含绝对路径，或 `main/yz_strings.h` 以外的手写源文件出现非 ASCII 界面字面量时失败。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

### 颜真卿字帖：《多宝塔碑》全碑字形包

| 文件 | 格式 | 用途与来源 |
| --- | --- | --- |
| [`images/yz_glyphs.bin`](images/yz_glyphs.bin) | 2025 字 × 128×128，4 bpp 灰度 + 游程编码（`YZG1`，约 4.2 MB） | 按碑文顺序收全碑每一个字：正文 2011 字，加偈颂七章后“其一”至“其七”14 个小字（一格并排两个小字，各存一字）。重复出现的字各保留自己的原拓。`main/CMakeLists.txt` 以 `EMBED_FILES` 嵌入 Flash，运行时只把当前字解码并双线性放大到一块 176×176 的 A8 缓冲（约 30 KB，另需两行源像素的临时空间），按“拓本 / 墨迹 / 描红”着色。分类卷与九个碑文卷引用同一批字形，不重复存储。 |
| [`images/yz_glyphs.manifest.json`](images/yz_glyphs.manifest.json) | JSON | 来源、存储 / 显示边长、每字哈希。 |

- 来源：开发者提供的“颜真卿《多宝塔碑》高清版”文件夹，剪裱本 42 页逐页图像（`01.jpg`–`42.jpg`，宽 700 像素，每页五行、行十字，个别页例外）。扫描件的原始出处未记载；底本为唐天宝十一载（752）碑刻拓本。图像不提交，逐页 SHA-256 记录在 `tools/yz_catalog.json` 与 manifest。在本项目之外发布衍生固件前，请先确认扫描件的再分发许可。
- 释文：[维基文库](https://zh.wikisource.org/wiki/%E8%A5%BF%E4%BA%AC%E5%8D%83%E7%A6%8F%E5%AF%BA%E5%A4%9A%E5%AF%B6%E4%BD%9B%E5%A1%94%E6%84%9F%E6%87%89%E7%A2%91)本（出自《全唐文》卷三七九），补入碑末题记。异体字以通行繁体标注（如“扵”作“於”、“寳”作“寶”），字形仍是碑上原刻。每字的碑文语境取自标点分句（不超过六字）。
- 切字：按投影找列，按字距先验用动态规划分字；平阙空格丢弃，朱印按色相剔除；第 1、2、19、22、27、42 页的不规则行用人工切点。逐页把释文叠在原图上逐字核对。
- 字目：[`tools/yz_catalog.json`](../tools/yz_catalog.json) 记录帖文资料、九个碑文卷（篇首、出家、建塔、赐额、舍利、塔相、法华、偈颂、题记）与五个分类卷，并逐字记录所属卷、分类、繁简体、拼音、结构、笔法重点、碑文语境、出处（“剪裱本 第几页 · 第几行”）、页码与像素框。841 个不同的字中，475 个的结构与笔法沿用此前人工核对的字目，其余 366 个据 IDS 字形分解得出并逐个复核。拼音按古读校正（如“佛”fó、纪年的“载”zǎi、“舍利”shèlì、“天台”tiāntāi、“琅邪”lángyá、“於戏”wūhū）。
- 处理：按像素框裁切 → 剔除朱印与 13 处人工标注的框（邻字残片、末页裱补纸）→ 单字自适应阈值（石面取外扩区 40 百分位，笔画取框内 97 百分位）→ 去掉小于约 0.25% 字格面积的石花噪点 → 按统一字格 130 像素缩放（保留字与字的相对大小）→ 以主体笔画的外接框居中放入 128×128 画布（即扫描件原有分辨率；远处零星的石花保留但不参与居中，保证每个字都对准米字格中心）→ 量化到 4 bpp。保留拓本肌理与泐损，不作描修。47 个大面积泐损的字标为 `damaged`：碑文卷照原样收录，分类卷在同一字另有写法时改用别处。
- 重新生成：`python3 tools/gen_yz_assets.py generate --src <含 01.jpg…42.jpg 的目录>`（需要 Pillow、numpy、scipy）；只改字目文字时用 `python3 tools/gen_yz_assets.py catalog`。`python3 tools/gen_yz_assets.py check`（已纳入 `tools/validate.sh --static`）只用标准库，逐字解码并核对字目与 manifest；`preview --out <png> [--range FROM TO]` 输出核对用的对照表。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
