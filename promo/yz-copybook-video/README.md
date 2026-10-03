<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Yan Zhenqing Copybook · 30-Second Vertical Promo

This project builds a short promo video for the AI Passport "Yan Zhenqing Copybook" play (the `feature/yan-zhenqing-copybook` branch). The video is 1080×1920, 30 fps, and 30 seconds long, for Douyin, Xiaohongshu, and WeChat Channels. It is a [Remotion](https://www.remotion.dev/) project, and code renders every frame deterministically.

Output: `out/yz-copybook-promo.mp4` (generated, not committed).

## Storyboard

| Time | Content |
| --- | --- |
| 0–1 s | Frame 0: the rubbing character *yong* ("eternal") slams in full-screen, with the hook line "characters from 1274 years ago" (the Duobao Pagoda Stele was erected in 752) |
| 1–3.5 s | 40 original stele characters cut on the beat, faster and faster, while a counter rolls from 0 to 1412 |
| 3.5–5.8 s | The last character becomes one cell of a 33×44 stele wall, and the camera pulls back fast to reveal all 1412 characters |
| 5.8–7 s | The device appears around the wall and shrinks with it. When it lands, the screen cuts to the real Thousand Character Classic catalog UI |
| 7–8.5 s | Three cuts: rubbing, ink, and red tracing. The whole background flips with each one |
| 8.5–10 s | Stele characters *Yan*, *Zhen*, and *Qing* slam in one by one, followed by the *Lugong* seal |
| 10–25 s | Features: three copybooks → stroke card → grids and themes → timed practice → full Thousand Character Classic → saved progress |
| 25–30 s | Brand title. In the last second, the camera pushes into *yong* on the screen. The last frame matches frame 0, so the video loops seamlessly |

`timeline.json` is the single source of timing for picture and sound (120 BPM, 15 frames per beat). The visuals (`src/`) and the music synthesizer (`scripts/synth_audio.py`) both read it, so a beat change needs only one edit.

## Rebuild

```bash
npm install
python3 -m venv .venv && .venv/bin/pip install numpy Pillow scipy
PY=.venv/bin/python ./scripts/build_assets.sh   # generate everything under public/gen/
npx remotion studio                             # preview
npx remotion render Promo out/raw.mp4 --crf 16
./scripts/finalize.sh                           # normalize loudness to −14 LUFS → out/yz-copybook-promo.mp4
```

`scripts/build_assets.sh` runs these steps in order:

| Script | Purpose |
| --- | --- |
| `extract_glyphs.py` | Reads `yz_glyphs.bin` from the copybook branch, decodes all 1412 glyphs, and builds the hero glyphs (4× upscale with added stone texture) and the wall atlas |
| `render_ui.sh` | Re-renders the 49 UI screenshots at 4× with the branch's real LVGL UI code |
| `make_device.sh` / `process_device.py` | Repaints a high-resolution device image with Bailian `bl image edit`, using `docs/brand/ai-passport-front.png` as the base. Then it mattes the background and locates the screen |
| `make_vo.sh` | Generates each voice-over line with MiMo TTS (voice *Baihua*). The script text is in `scripts/vo_lines.json` |
| `synth_audio.py` | Synthesizes the original music and sound effects, and ducks the music under the voice-over |
| `fetch_fonts.py` | Downloads Noto Serif SC / Noto Sans SC subsets that contain only the characters used on screen |

After you change the voice-over text, rerun `make_vo.sh` and `synth_audio.py`. After you change on-screen text, rerun `fetch_fonts.py`.

## Video covers

`./scripts/render_covers.sh` writes 8 covers to `out/covers/` (PNG and JPG). The layouts are in `src/covers/Covers.tsx`.

| Style | Content |
| --- | --- |
| A, stele forest | Dark rubbing background, faint 1412-character wall, stele characters *Yan Zhen Qing* stacked vertically, device home screen |
| B, red tracing | Paper background, a grid cell with *yong* in tracing red, device tracing screen |

| Ratio | Size | Suggested use | Layout notes |
| --- | --- | --- | --- |
| 16:9 | 1920×1080 | Bilibili, Douyin landscape videos | Bilibili shows duration at bottom right and play count at bottom left, so key text stays out of both bottom corners |
| 4:3 | 1440×1080 | Xiaohongshu landscape notes | — |
| 3:4 | 1080×1440 | Xiaohongshu portrait notes (preferred) | The title is about 14% of the image width per character, so it stays readable in feed thumbnails |
| 9:16 | 1080×1920 | Douyin portrait videos | The Douyin profile grid crops covers to 3:4, so the title and the main visual (stele characters or tracing cell) sit in the central 1080×1440 area. In style B, the bottom edge of the device is cropped slightly |

## Sources and licenses

- **Stele glyphs**: from `assets/images/yz_glyphs.bin` on the copybook branch. The Duobao Pagoda Stele and Yan Qinli Stele rubbings are public domain through Wikimedia Commons. The Thousand Character Classic is an 1884 woodblock edition marked Public Domain Mark by the National Diet Library of Japan. Its attribution to Yan Zhenqing is doubtful, so the copy says "original stele characters" and never "authentic handwriting".
- **UI screenshots**: rendered on the host from the branch's UI code, and identical to what the device displays.
- **Device image**: AI-generated (Bailian qwen-image) from the official reference in `docs/brand`. Following the brand rules, only the screen area was repainted.
- **Music and sound effects**: synthesized in code by `synth_audio.py` (Karplus-Strong plucks, taiko, and similar). No samples or third-party music are used.
- **Voice-over**: Xiaomi MiMo-V2.5-TTS.
- **Fonts**: Noto Serif SC / Noto Sans SC (Source Han Serif / Sans), SIL Open Font License 1.1.
- **Brand wordmark**: `assets/images/logo-wordmark-dark.png`.
