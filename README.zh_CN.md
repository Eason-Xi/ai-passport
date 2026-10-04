<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 乐器节拍器 — FoloToy AI Passport

为 FoloToy AI Passport（ESP32-C3、240 × 320 屏幕、三个按键、ES8311 扬声器）打造的离线乐器节拍器。
界面为简体中文，click 声以采样级精度排布，长时间运行不漂移；画面上的节拍点与摆杆在声音真正
从扬声器发出的那一刻才点亮。

本 `feature/metronome` 分支用全新设计的节拍器界面替换了基线硬件测试菜单：开机直接进入节拍器，
不会再出现测试菜单或 demo 页面。基线 `demo_*.c` 文件仍留在仓库中供主机测试使用，但不编译进固件。

<p align="center">
  <img src="assets/images/metronome-preview.png" alt="节拍器界面：演奏中、7/4 三连音、设置面板、敲击测速" width="100%">
</p>

> 预览图由 `tools/render_metronome_preview.py` 在主机上用真实 LVGL 界面代码渲染，不是真机照片。

## 功能

| 功能 | 说明 |
| --- | --- |
| 速度 | 30–250 BPM；按一下 ±1，按住加速连调（先 ±1，约 2 秒后按 5 的倍数跳），松手立即停 |
| 拍号与重音 | 每小节 1–12 拍，首拍重音可开关 |
| 拍子细分 | 不细分 / 八分 / 三连音 / 十六分，细分音音量更轻 |
| 敲击测速 | 跟着音乐按任意键，取最近至多 8 个间隔的平均值；停下 2.5 秒后自动应用 |
| 音色 | 电子音、木块、牛铃，均为固件内合成（重音 / 普通拍 / 细分三种力度） |
| 音量 | 0–10 档（0 为静音）|
| 速度术语 | 按 BPM 显示意大利语术语与中文名，例如 Andante · 行板 |
| 掉电保存 | 设置保存在 NVS；为避免爆音只在停止时写入 Flash |
| 省电 | 停止且 60 秒无操作时背光降到 10%，任意键唤醒（唤醒这一下不触发功能）|
| 电量 | 右上角显示电量计读数；不可用时隐藏 |

## 按键

| 页面 | UP / DOWN | OK 单击 | OK 双击 | OK 长按 |
| --- | --- | --- | --- | --- |
| 主界面 | 按下 ±1 BPM；按住加速连调 | 开始 / 停止 | 敲击测速 | 打开设置 |
| 设置（浏览） | 移动选中行 | 编辑该行（"敲击测速"行直接进入）| — | 返回主界面 |
| 设置（编辑） | 改值（拍号、音量可按住连调）| 确认 | — | 返回主界面 |
| 敲击测速 | 任意键按下 = 敲击 | （同为敲击）| — | 取消，不改速度 |

设置面板打开时节拍器继续播放，改动立即可听；面板头部的指示灯随节拍闪烁。进入敲击测速时
会暂停播放，结束后恢复原来的播放状态。

## 计时如何做到精确

- **样本即时钟**：音频任务（优先级 6）每 15 ms 渲染一块 240 样本并阻塞写入 I2S。`main/mn_sched.c`
  用"整数商 + 余数累加"计算每个 tick 的样本位置，第 k 个 tick 恰好落在
  `floor(k × 16000 × 60 / (BPM × 细分))`，误差永远小于一个样本（62.5 µs），没有累积漂移。
- **声画同步**：开始时先写满整个 DMA 输出队列（6 × 240 样本 ≈ 90 ms）的静音，之后队列保持满载，
  于是每个 tick 的发声时刻可由写入返回时间推算。界面在 LVGL 任务的 20 ms 定时器里取出已到发声时刻的事件，
  再点亮节拍点、让摆锤闪光。摆杆按余弦运动，每个拍头到达一侧极点。
- **参数切换**：BPM 立即按新间隔重排；细分在下一拍拍头生效；拍数缩小时在下一拍回到第 1 拍。
- **不打断音频**：设置只在节拍器停止、最后一块 PCM 播完后才写 Flash，运行中的修改在停止后落盘。

## 构建与刷写

需要 ESP-IDF 5.5.3（见[环境准备](docs/development/engineering/environment-setup.zh_CN.md)）。

```bash
source ~/esp/esp-idf-v5.5.3/export.sh
./tools/validate.sh            # 仓库检查 + 主机测试 + 固件构建 + 合并镜像校验
```

交付固件为 `build/FoloToy-AI-Passport-full.bin`，从 `0x0` 烧录：

```bash
python -m esptool --chip esp32c3 -p <端口> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

合并镜像会覆盖 NVS，已保存的设置会恢复默认值，详见[烧录说明](docs/development/engineering/firmware-layout.zh_CN.md#烧录与已存数据)。
匹配的 ELF / MAP 归档在 `build/firmware/<full.bin 的 sha256>/`。

修改界面文案后需重新生成字体子集（见 [`assets/README.zh_CN.md`](assets/README.zh_CN.md)）；
`python3 tools/render_metronome_preview.py` 可在主机上渲染所有页面并报告 LVGL 内存池峰值。

## 代码结构

| 文件 | 作用 |
| --- | --- |
| `main/mn_sched.*` | 采样级节拍调度与单声部混音（纯逻辑）|
| `main/mn_sound.*` | 三种音色 × 三种力度的 click 合成（纯逻辑）|
| `main/mn_tap.*` | 敲击测速（纯逻辑）|
| `main/mn_tempo.*`、`main/mn_layout.*` | 速度术语、长按连调节奏、节拍点排布、摆杆角度（纯逻辑）|
| `main/mn_cfg.*`、`main/mn_crc32.*` | 设置默认值、范围与带 CRC 的存档格式（纯逻辑）|
| `main/mn_model.*` | 页面状态机、按键映射、熄屏唤醒（纯逻辑）|
| `main/mn_audio.*` | 音频任务、声画同步事件 |
| `main/mn_store.*` | NVS 存储 |
| `main/mn_app.*`、`main/main.c` | 应用任务与固件入口 |
| `main/mn_ui*.c`、`main/mn_theme.h`、`main/mn_strings.h`、`main/mn_fonts.*` | 界面、配色、全部中文文案与字体自检 |
| `components/bsp` | 复用的板级驱动；本分支新增按键 `BSP_BTN_RELEASE` 事件并公开 I2S DMA 队列尺寸 |
| `tests/test_mn_*.c`、`tests/test_metronome_fonts.py` | 主机测试，由 `tools/validate.sh --static` 运行 |

## 已知限制与待真机确认

- 声画同步以"DMA 队列始终满载"推算发声时刻，实际偏差需在真机上目测确认；
  如有固定偏差可在 `mn_ui.c` 调整补偿。
- 音量档位到 codec 百分比的映射（1 档 46% … 10 档 100%）是初值，响度曲线需真机试听。
- 敲击测速精度受按键 5 ms 轮询影响，单次间隔约有 ±5 ms 抖动，平均后误差约 ±1 BPM。
- 三个按键共用一路 ADC，不支持组合键。
