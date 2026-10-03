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

### Yan Zhenqing copybook (Duobao Pagoda Stele) fonts

| File | Size / bpp | Characters | Use |
| --- | --- | --- | --- |
| [`fonts/yz_zh14.c`](fonts/yz_zh14.c) | 14 px / 4 bpp | Every string literal in `main/yz_strings.h` + all displayed text in `tools/yz_catalog.json` (book name, era, author, introduction, rubbing source, volume names, and each character's simplified form, traditional form, pinyin, stele context, and location) + printable ASCII, about 1,420 characters | Body text, hints, tabs, technique card, battery |
| [`fonts/yz_zh18.c`](fonts/yz_zh18.c) | 18 px / 4 bpp | Same as `yz_zh14` | Menus, header rows, volume names, pinyin |
| [`fonts/yz_zh24.c`](fonts/yz_zh24.c) | 24 px / 4 bpp | Same as `yz_zh14` | Catalog cells (stele characters in standard traditional form), page titles, timer |
| [`fonts/yz_zh32.c`](fonts/yz_zh32.c) | 32 px / 4 bpp | Book name, simplified forms of all catalog characters, digits (849 characters) | Vertical book name, large simplified character |

- Source: Adobe Source Han Sans SC 2.005 (`SourceHanSansSC-Regular.otf`, SubsetOTF/SC), SIL Open Font License 1.1. The OTF is not committed; its SHA-256 is recorded in [`fonts/yz_fonts.manifest.json`](fonts/yz_fonts.manifest.json).
- Converter: `lv_font_conv` 1.5.3 with `--no-compress --no-kerning`, LVGL 9.5 format. The manifest records each font's full command, code-point ranges, and output hash.
- Regenerate after changing UI text or the catalog: `python3 tools/gen_yz_fonts.py generate --lv-font-conv <lv_font_conv> --font <SourceHanSansSC-Regular.otf>` (requires fontTools). The generator also rewrites `main/yz_font_glyphs.h`, which the firmware uses for a per-code-point self-check at startup.
- `python3 tools/gen_yz_fonts.py check` (part of `tools/validate.sh --static`) uses only the standard library: it parses the committed fonts' cmaps and fails on missing glyphs, stale files, absolute paths, or non-ASCII UI literals in handwritten sources other than `main/yz_strings.h`.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

### Yan Zhenqing copybook: complete Duobao Pagoda Stele glyph pack

| File | Format | Use and source |
| --- | --- | --- |
| [`images/yz_glyphs.bin`](images/yz_glyphs.bin) | 2,025 characters × 128×128, 4 bpp grayscale + run-length encoding (`YZG1`, about 4.2 MB) | Every character of the stele in reading order: 2,011 characters of the main text plus the 14 small verse numbers (*qi yi* to *qi qi*) after the seven verses (one cell holds two small characters side by side; each is stored separately). Repeated characters keep their own rubbing. `main/CMakeLists.txt` embeds the pack in Flash with `EMBED_FILES`; at runtime only the current character is decoded and bilinearly upscaled into one 176×176 A8 buffer (about 30 KB, two source rows of scratch space) and recolored for the stone, ink, or tracing style. The category volumes reference the same glyphs as the nine reading-order volumes, so nothing is stored twice. |
| [`images/yz_glyphs.manifest.json`](images/yz_glyphs.manifest.json) | JSON | Source, store / display sizes, and per-glyph hashes. |

- Source: the developer-supplied high-resolution scan folder of the stele, a cut-and-mounted rubbing album photographed as 42 page images (`01.jpg`–`42.jpg`, 700 px wide, five columns of ten characters per page with a few exceptions). The original publisher of the scans is not recorded; the underlying work is the Tang-dynasty stele (752). The images are not committed; per-page SHA-256 values are recorded in `tools/yz_catalog.json` and the manifest. Confirm redistribution rights for the scans before publishing a derived build outside this project.
- Text: the transcription on [Wikisource](https://zh.wikisource.org/wiki/%E8%A5%BF%E4%BA%AC%E5%8D%83%E7%A6%8F%E5%AF%BA%E5%A4%9A%E5%AF%B6%E4%BD%9B%E5%A1%94%E6%84%9F%E6%87%89%E7%A2%91) (from *Quan Tang Wen*, juan 379), with the colophon lines added. Variant forms are written in their standard traditional form; the glyph always shows the carved form. The clause punctuation provides each character's stele context (at most six characters).
- Segmentation: columns are found by projection and characters by dynamic programming with a character-pitch prior; blank honorific spaces are dropped and seals are rejected by hue. Irregular columns on pages 1, 2, 19, 22, 27, and 42 use hand-placed cuts. Every page was overlaid with the transcription and checked character by character.
- Catalog: [`tools/yz_catalog.json`](../tools/yz_catalog.json) records the book text, the nine reading-order volumes (opening, ordination, building the pagoda, imperial plaque, relics, the pagoda, Lotus teaching, verses, colophon) and five category volumes, and each character's volume, category, traditional and simplified forms, pinyin, structure, brush-technique focus, stele context, location (album page and column), page, and pixel box. Structure and focus for 475 of the 841 distinct characters come from the previous hand-checked catalog; the other 366 were derived from IDS decompositions and reviewed by hand. Pinyin uses classical readings where they differ (for example *fó* for Buddha, *zǎi* when counting years, *shèlì* for relics, *Tiāntāi*, *Lángyá*, and *wūhū*).
- Processing: crop by pixel box → mask vermilion seals and 13 hand-marked boxes (neighbor fragments and the mounting paper on the last page) → per-character adaptive threshold (40th-percentile background of a widened region, 97th-percentile stroke level) → remove stone-flaw specks smaller than about 0.25% of a character cell → scale by a uniform 130 px cell so relative character sizes are preserved → center on a 128×128 canvas (the scans' native resolution) → quantize to 4 bpp. Rubbing texture and stone damage are kept; nothing is retouched. Forty-seven characters with large damaged areas are marked `damaged`: they stay as carved in the reading-order volumes, while the category volumes pick another occurrence of the same character when one exists.
- Regenerate with `python3 tools/gen_yz_assets.py generate --src <directory with 01.jpg…42.jpg>` (requires Pillow, numpy, scipy); use `python3 tools/gen_yz_assets.py catalog` when only catalog text changes. `python3 tools/gen_yz_assets.py check` (part of `tools/validate.sh --static`) uses only the standard library to decode every glyph and compare the pack with the catalog and manifest; `preview --out <png> [--range FROM TO]` writes a contact sheet for review.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
