<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 颜真卿字帖 · 30 秒竖屏推广视频

AI Passport「颜真卿字帖」玩法（`feature/yan-zhenqing-copybook` 分支）的推广短视频工程，规格为 1080×1920、30 fps、30 秒，适用于抖音、小红书、视频号。工程基于 [Remotion](https://www.remotion.dev/)，每一帧都由代码确定性地渲染。

成片：`out/yz-copybook-promo.mp4`（生成文件，不入库）。

## 分镜

| 时间 | 内容 |
| --- | --- |
| 0–1 s | 第 0 帧，拓本「永」满屏砸入，配钩子文案「1274 年前的字」（多宝塔碑立于 752 年） |
| 1–3.5 s | 40 个原碑字随鼓点闪切，越切越快，计数从 0 滚到 1412 |
| 3.5–5.8 s | 最后一个字成为 33×44 碑墙中的一格，镜头急速拉远，露出全部 1412 字 |
| 5.8–7 s | 设备出现在碑墙四周，带着碑墙一起缩小，落定后切到千字文目录的真实界面 |
| 7–8.5 s | 拓本、墨迹、描红三连切，整屏底色随之翻转 |
| 8.5–10 s | 原碑集字「顏」「真」「卿」逐个砸出，盖「鲁公」印 |
| 10–25 s | 功能段：三本字帖 → 笔法卡 → 格线与底色 → 计时临写 → 千字文全文 → 进度保存 |
| 25–30 s | 品牌标题；最后 1 秒推进屏幕里的「永」，末帧与第 0 帧完全一致，可无缝循环播放 |

`timeline.json` 是音画共用的唯一时间源（120 BPM，每拍 15 帧）。画面（`src/`）和配乐合成（`scripts/synth_audio.py`）都读取它，修改节拍时只需要改这一处。

## 重新生成

```bash
npm install
python3 -m venv .venv && .venv/bin/pip install numpy Pillow scipy
PY=.venv/bin/python ./scripts/build_assets.sh   # 生成 public/gen/ 下的全部素材
npx remotion studio                             # 预览
npx remotion render Promo out/raw.mp4 --crf 16
./scripts/finalize.sh                           # 响度归一到 −14 LUFS → out/yz-copybook-promo.mp4
```

`scripts/build_assets.sh` 依次执行以下步骤：

| 脚本 | 作用 |
| --- | --- |
| `extract_glyphs.py` | 从字帖分支读取 `yz_glyphs.bin`，解码出 1412 个字形，生成主角字（放大 4 倍并补石花纹理）和碑墙图集 |
| `render_ui.sh` | 用字帖分支里真实的 LVGL 界面代码，以 4 倍分辨率重新渲染 49 张界面截图 |
| `make_device.sh` / `process_device.py` | 用百炼 `bl image edit` 以 `docs/brand/ai-passport-front.png` 为基底重绘高清设备图，然后抠出背景、定位屏幕区域 |
| `make_vo.sh` | MiMo TTS（音色「白桦」）逐句生成旁白，文案在 `scripts/vo_lines.json` |
| `synth_audio.py` | 合成原创配乐和音效，并在旁白处自动压低配乐 |
| `fetch_fonts.py` | 只下载画面上用到的字，生成思源宋体 / 思源黑体子集 |

修改旁白文案后，需要重跑 `make_vo.sh` 和 `synth_audio.py`；修改画面文字后，需要重跑 `fetch_fonts.py`。

## 视频封面

`./scripts/render_covers.sh` 输出 8 张封面到 `out/covers/`（PNG 与 JPG 各一份），版式在 `src/covers/Covers.tsx`。

| 风格 | 内容 |
| --- | --- |
| A「碑林」 | 拓本深色底 + 1412 字碑墙暗纹 + 原碑集字「顏真卿」竖排 + 设备主页 |
| B「描红」 | 纸色底 + 米字格描红「永」+ 设备描红界面 |

| 比例 | 尺寸 | 建议用途 | 版式注意 |
| --- | --- | --- | --- |
| 16:9 | 1920×1080 | 哔哩哔哩、抖音横屏视频 | B 站右下角是时长、左下角是播放量，底部两角不放关键文字 |
| 4:3 | 1440×1080 | 小红书横版笔记 | — |
| 3:4 | 1080×1440 | 小红书竖版笔记（首选） | 标题字号约占画宽 14%，信息流缩略图里也读得清 |
| 9:16 | 1080×1920 | 抖音竖屏视频 | 抖音主页网格按 3:4 裁切显示，标题和主视觉（集字或描红格）都在中间 1080×1440 区域内；B 套的设备下沿会被裁掉一点 |

## 素材来源与许可

- **碑帖字形**：取自字帖分支的 `assets/images/yz_glyphs.bin`。《多宝塔碑》和《颜勤礼碑》的拓本经 Wikimedia Commons 以公有领域发布；《千字文》为 1884 年刻本，日本国立国会图书馆标注为 Public Domain Mark，传为颜真卿所书，真伪存疑。因此文案统一使用「碑帖原字」，不使用「真迹」。
- **界面截图**：由字帖分支的界面代码在主机上渲染，与真机显示一致。
- **设备图**：AI 生成（百炼 qwen-image），基底是 `docs/brand` 中的官方参考图，按品牌规范只重绘了屏幕区域。
- **配乐与音效**：由 `synth_audio.py` 用代码合成（Karplus-Strong 拨弦、太鼓等），没有使用任何采样或第三方音乐。
- **旁白**：小米 MiMo-V2.5-TTS 生成。
- **字体**：Noto Serif SC / Noto Sans SC（即思源宋体 / 思源黑体），SIL Open Font License 1.1。
- **品牌字标**：`assets/images/logo-wordmark-dark.png`。
