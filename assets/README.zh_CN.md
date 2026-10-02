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
| [`fonts/yz_zh14.c`](fonts/yz_zh14.c) | 14 px / 4 bpp | `main/yz_strings.h` 全部字符串字面量 + `tools/yz_catalog.json` 的简体、繁体、拼音、碑文语境 + 可打印 ASCII | 正文、提示、页签、笔法卡、电量 |
| [`fonts/yz_zh18.c`](fonts/yz_zh18.c) | 18 px / 4 bpp | 同 `yz_zh14` | 菜单、标题行、拼音 |
| [`fonts/yz_zh24.c`](fonts/yz_zh24.c) | 24 px / 4 bpp | 同 `yz_zh14` | 目录字格（碑上原字）、页面标题、计时 |
| [`fonts/yz_zh32.c`](fonts/yz_zh32.c) | 32 px / 4 bpp | 字目的简体与繁体单字、`YZ_STR_APP_TITLE`、数字 | 竖排题名、简体大字 |

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

### 颜真卿字帖：《多宝塔碑》拓本字形包

| 文件 | 格式 | 用途与来源 |
| --- | --- | --- |
| [`images/yz_glyphs.bin`](images/yz_glyphs.bin) | 188 字 × 176×176，4 bpp 灰度 + 游程编码（`YZG1`，约 666 KB） | 字帖的拓本原字。`main/CMakeLists.txt` 以 `EMBED_FILES` 嵌入 Flash，运行时只把当前字解码到一块 30 KB 的 A8 缓冲，按“拓本 / 墨迹 / 描红”着色。 |
| [`images/yz_glyphs.manifest.json`](images/yz_glyphs.manifest.json) | JSON | 来源、处理参数、每字哈希。 |

- 来源：宋拓《多寶佛塔碑》冊，台北故宫博物院藏（故帖 000019），[Wikimedia Commons](https://commons.wikimedia.org/wiki/File:NPM-%E6%95%85%E5%B8%96000019_%E5%AE%8B%E6%8B%93%E5%A4%9A%E5%AF%B6%E4%BD%9B%E5%A1%94%E7%A2%91_%E5%86%8A.pdf) 标注为公有领域（唐代书迹的平面复制品）。PDF（约 33 MB）不提交，SHA-256 记录在 manifest。
- 字目：[`tools/yz_catalog.json`](../tools/yz_catalog.json) 逐字记录章节、简体、拼音、结构、笔法重点、碑文语境、拓本页码与像素框；全部经人工对照拓本核对。
- 处理：按像素框裁切 → 单字自适应阈值（背景取中位数、笔画取 97 百分位）→ 去掉小于约 0.25% 字格面积的石花噪点 → 按版心字格统一缩放（保留字与字的相对大小）→ 居中放入 176×176 画布 → 量化到 4 bpp。保留拓本肌理与残损，不作描修。
- 重新生成：`python3 tools/gen_yz_assets.py generate --pdf <拓本 PDF>`（需要 pymupdf、Pillow、numpy、scipy）；只改字目文字时用 `python3 tools/gen_yz_assets.py catalog`。`python3 tools/gen_yz_assets.py check`（已纳入 `tools/validate.sh --static`）只用标准库，逐字解码并核对字目与 manifest；`preview --out <png>` 输出核对用的对照表。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
