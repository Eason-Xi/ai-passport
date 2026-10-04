<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Limited Rock-Paper-Scissors font subsets

| File | Source / size / bpp | Characters | Use |
| --- | --- | --- | --- |
| [`fonts/kj_zh14.c`](fonts/kj_zh14.c) | Source Han Sans SC Regular, 14 px, 4 bpp | Every string literal in `main/kj_strings.h` plus printable ASCII (290 glyphs) | Hints, footers, small labels |
| [`fonts/kj_zh18.c`](fonts/kj_zh18.c) | Source Han Sans SC Bold, 18 px, 4 bpp | Same as `kj_zh14` | Body text, list rows, buttons |
| [`fonts/kj_zh26.c`](fonts/kj_zh26.c) | Source Han Sans SC Heavy, 26 px, 4 bpp | Same as `kj_zh14` | Page titles, stars, host statistics |
| [`fonts/kj_big48.c`](fonts/kj_big48.c) | Source Han Sans SC Heavy, 48 px, 4 bpp | Only the `KJ_BIG_*` strings (18 glyphs) | Title, challenge banner, win / lose / draw, cleared / out / failed |
| [`fonts/kj_num56.c`](fonts/kj_num56.c) | Source Han Sans SC Heavy, 56 px, 4 bpp | `0`–`9`, `A`–`F`, `-` (17 glyphs) | Seat numbers and the host's game code |
| [`fonts/kj_hand36.c`](fonts/kj_hand36.c) / [`fonts/kj_hand64.c`](fonts/kj_hand64.c) | Noto Emoji (static `wght=700` instance), 36 / 64 px, 4 bpp | U+270A, U+270B, U+270C (rock, paper, scissors hands) | Card faces |

- Sources: Source Han Sans SC 2.005 (`OTF/SimplifiedChinese/SourceHanSansSC-{Regular,Bold,Heavy}.otf`,
  SIL Open Font License 1.1, <https://github.com/adobe-fonts/source-han-sans>) and Noto Emoji
  (`NotoEmoji[wght].ttf`, SIL Open Font License 1.1, <https://github.com/google/fonts/tree/main/ofl/notoemoji>).
  The source font files are not committed; their SHA-256 values and the exact converter command and
  code-point ranges of every subset are recorded in [`fonts/kj_fonts.manifest.json`](fonts/kj_fonts.manifest.json).
  The generated subsets are redistributed under the same license; the license texts are kept in
  [`fonts/LICENSE-SourceHanSans.txt`](fonts/LICENSE-SourceHanSans.txt) and
  [`fonts/LICENSE-NotoEmoji.txt`](fonts/LICENSE-NotoEmoji.txt), and the subsets use their own names
  (`kj_*`) instead of the Reserved Font Name.
- Converter: `lv_font_conv` 1.5.3 with `--bpp 4 --no-compress --no-kerning --format lvgl --lv-include lvgl.h`.
  The generator instantiates Noto Emoji at `wght=700` with fontTools before converting.
- Regenerate after changing any UI text:
  `python3 tools/gen_kj_fonts.py generate --lv-font-conv <lv_font_conv> --font-dir <directory with the four source fonts>`.
  `python3 tools/gen_kj_fonts.py check` (run by `tools/validate.sh`) fails when the committed fonts,
  the manifest, or `main/kj_font_glyphs.h` no longer match the strings, or when a non-ASCII display
  literal appears outside `main/kj_strings.h`.
- `main/CMakeLists.txt` compiles `assets/fonts/kj_*.c`; the firmware runs `kj_fonts_selfcheck()` at boot and
  logs any missing or placeholder glyph, including a negative check that must fail.
- Supported text is fixed: devices only display the strings in `main/kj_strings.h`, ASCII digits and
  hexadecimal game codes. Player names exist only on the computer dashboard, which uses system fonts.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`images/kj-preview.png`](images/kj-preview.png) | 1524 × 344, PNG RGB | Limited Rock-Paper-Scissors README preview: six device screens rendered on the host by `tools/render_kj_preview.py` with the real UI code and fonts, then tiled. Not used by the firmware. |
| [`images/kj-board-preview.png`](images/kj-board-preview.png) | 1050 × 750, PNG RGB | Screenshot of `tools/kj_board/index.html` in headless Chromium, fed with board lines produced by the real `main/kj_board.c` from a scripted game. Not used by the firmware. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
