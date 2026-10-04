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

### 限定猜拳字体子集

| 文件 | 源字体 / 字号 / bpp | 字符 | 用途 |
| --- | --- | --- | --- |
| [`fonts/kj_zh14.c`](fonts/kj_zh14.c) | 思源黑体 SC Regular，14 px，4 bpp | `main/kj_strings.h` 里的全部字符串 + 可打印 ASCII（290 字） | 提示、页脚、小字 |
| [`fonts/kj_zh18.c`](fonts/kj_zh18.c) | 思源黑体 SC Bold，18 px，4 bpp | 同 `kj_zh14` | 正文、列表、按钮 |
| [`fonts/kj_zh26.c`](fonts/kj_zh26.c) | 思源黑体 SC Heavy，26 px，4 bpp | 同 `kj_zh14` | 页面标题、星星、庄家统计 |
| [`fonts/kj_big48.c`](fonts/kj_big48.c) | 思源黑体 SC Heavy，48 px，4 bpp | 仅 `KJ_BIG_*` 字符串（18 字） | 标题、挑战横幅、胜 / 负 / 平、过关 / 出局 / 失败 |
| [`fonts/kj_num56.c`](fonts/kj_num56.c) | 思源黑体 SC Heavy，56 px，4 bpp | `0`–`9`、`A`–`F`、`-`（17 字） | 选手编号与庄家赌局号 |
| [`fonts/kj_hand36.c`](fonts/kj_hand36.c) / [`fonts/kj_hand64.c`](fonts/kj_hand64.c) | Noto Emoji（`wght=700` 静态实例），36 / 64 px，4 bpp | U+270A、U+270B、U+270C（石头、布、剪刀手势） | 牌面 |

- 来源：思源黑体 SC 2.005（`OTF/SimplifiedChinese/SourceHanSansSC-{Regular,Bold,Heavy}.otf`，
  SIL Open Font License 1.1，<https://github.com/adobe-fonts/source-han-sans>）与 Noto Emoji
  （`NotoEmoji[wght].ttf`，SIL Open Font License 1.1，<https://github.com/google/fonts/tree/main/ofl/notoemoji>）。
  源字体文件不入库；其 SHA-256、每个子集的完整转换命令与码点范围记录在
  [`fonts/kj_fonts.manifest.json`](fonts/kj_fonts.manifest.json)。
  生成的子集按同一许可再分发，许可全文见 [`fonts/LICENSE-SourceHanSans.txt`](fonts/LICENSE-SourceHanSans.txt)
  与 [`fonts/LICENSE-NotoEmoji.txt`](fonts/LICENSE-NotoEmoji.txt)；子集使用自己的名字（`kj_*`），不使用保留字体名。
- 转换器：`lv_font_conv` 1.5.3，参数 `--bpp 4 --no-compress --no-kerning --format lvgl --lv-include lvgl.h`。
  生成脚本先用 fontTools 把 Noto Emoji 切成 `wght=700` 的静态实例再转换。
- 修改任何界面文字后重新生成：
  `python3 tools/gen_kj_fonts.py generate --lv-font-conv <lv_font_conv> --font-dir <放着四个源字体的目录>`。
  `python3 tools/gen_kj_fonts.py check`（`tools/validate.sh` 会运行）在已提交的字体、manifest 或
  `main/kj_font_glyphs.h` 与文字不一致，或 `main/kj_strings.h` 之外出现非 ASCII 显示字面量时失败。
- `main/CMakeLists.txt` 编译 `assets/fonts/kj_*.c`；固件开机运行 `kj_fonts_selfcheck()`，逐个记录缺字或占位框，
  并包含一个必须查不到的反例。
- 支持的文字是固定的：设备只显示 `main/kj_strings.h` 中的字符串、ASCII 数字和十六进制赌局号。选手名字只存在于
  电脑看板，由系统字体显示。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/kj-preview.png`](images/kj-preview.png) | 1524 × 344，PNG RGB | 限定猜拳 README 预览图：`tools/render_kj_preview.py` 在电脑上用真实界面代码与字体渲染的六个设备页面拼接而成。固件不使用。 |
| [`images/kj-board-preview.png`](images/kj-board-preview.png) | 1050 × 750，PNG RGB | `tools/kj_board/index.html` 在无头 Chromium 中的截图，输入是真实 `main/kj_board.c` 按脚本对局生成的看板行。固件不使用。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
