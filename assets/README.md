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

| File | Size / bpp | Characters | Use |
| --- | --- | --- | --- |
| [`fonts/mn_zh14.c`](fonts/mn_zh14.c) | 14 px, 4 bpp | All string literals in `main/mn_strings.h` plus printable ASCII (194 glyphs) | Metronome hints, top bar, small labels |
| [`fonts/mn_zh18.c`](fonts/mn_zh18.c) | 18 px, 4 bpp | Same character set as `mn_zh14` | Metronome tempo term, settings rows, titles |
| [`fonts/mn_num88.c`](fonts/mn_num88.c) | 88 px, 4 bpp | `0`–`9` and `-` (11 glyphs) | Metronome BPM numerals and the tap-tempo placeholder |

- Source: Source Han Sans SC 2.005 Regular (`SourceHanSansSC-Regular.otf`, SIL Open Font License 1.1, <https://github.com/adobe-fonts/source-han-sans>). The OTF itself is not committed; its SHA-256 is recorded in [`fonts/mn_fonts.manifest.json`](fonts/mn_fonts.manifest.json).
- Converter: `lv_font_conv` 1.5.3 with `--bpp 4 --no-compress --no-kerning --format lvgl --lv-include lvgl.h`; the manifest stores the exact command and code-point ranges for every font.
- Regenerate after changing any UI text: `python3 tools/gen_metronome_fonts.py generate --lv-font-conv <lv_font_conv> --font <SourceHanSansSC-Regular.otf>`. `python3 tools/gen_metronome_fonts.py check` (run by `tools/validate.sh`) fails when the committed fonts, manifest, or `main/mn_font_glyphs.h` no longer match the strings.
- `main/CMakeLists.txt` adds `assets/fonts/mn_*.c` with `target_sources`; the firmware runs `mn_fonts_selfcheck()` at boot and logs any missing or placeholder glyph.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`images/metronome-preview.png`](images/metronome-preview.png) | 1020 × 344, PNG RGB | Metronome README preview: four screens (playing, 7/4 with triplets, settings while editing, tap tempo) rendered on the host by `tools/render_metronome_preview.py` with the real LVGL UI code and fonts, then tiled; screen corners are masked like the BSP. Not used by the firmware. |

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
