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

### Yan Zhenqing copybook (Duobao Pagoda Stele, Yan Qinli Stele, Thousand Character Classic) fonts

| File | Size / bpp | Characters | Use |
| --- | --- | --- | --- |
| [`fonts/yz_zh14.c`](fonts/yz_zh14.c) | 14 px / 4 bpp | Every string literal in `main/yz_strings.h` + all displayed text in `tools/yz_catalog.json` (book names, eras, authors, introductions, rubbing sources, chapter names, and each character's simplified form, traditional form, pinyin, source-text context, and location) + printable ASCII, about 1,900 characters | Body text, hints, tabs, technique card, battery |
| [`fonts/yz_zh18.c`](fonts/yz_zh18.c) | 18 px / 4 bpp | Same as `yz_zh14` | Menus, header rows, pinyin |
| [`fonts/yz_zh24.c`](fonts/yz_zh24.c) | 24 px / 4 bpp | Same as `yz_zh14` | Catalog cells (original stele characters), page titles, timer |
| [`fonts/yz_zh32.c`](fonts/yz_zh32.c) | 32 px / 4 bpp | Book names, simplified catalog characters, digits | Vertical book name, large simplified character |

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

### Yan Zhenqing copybook: Duobao Pagoda Stele, Yan Qinli Stele, and Thousand Character Classic glyph pack

| File | Format | Use and source |
| --- | --- | --- |
| [`images/yz_glyphs.bin`](images/yz_glyphs.bin) | 1,412 characters (188 Duobao + 224 Qinli + 1,000 Thousand Character Classic) × 176×176, 4 bpp grayscale + run-length encoding (`YZG1`, about 4.2 MB) | The copybooks' original characters, one book after the other. `main/CMakeLists.txt` embeds it in Flash with `EMBED_FILES`; at runtime only the current character is decoded into one 30 KB A8 buffer and recolored for the stone, ink, or tracing style. New books are only appended so that character indices in existing saves stay valid. The Thousand Character Classic's category volumes reference the same glyphs as its full-text volume, so nothing is stored twice. |
| [`images/yz_glyphs.manifest.json`](images/yz_glyphs.manifest.json) | JSON | Source, processing parameters, and per-glyph hashes. |

- Sources (all marked public domain on [Wikimedia Commons](https://commons.wikimedia.org/) as flat reproductions of Tang-dynasty calligraphy; the PDFs are not committed and their SHA-256 values are recorded in `tools/yz_catalog.json` and the manifest):
  - Duobao Pagoda Stele: Song-dynasty rubbing album, National Palace Museum, Taipei (object no. Gutie 000019), [PDF, about 33 MB](https://commons.wikimedia.org/wiki/File:NPM-%E6%95%85%E5%B8%96000019_%E5%AE%8B%E6%8B%93%E5%A4%9A%E5%AF%B6%E4%BD%9B%E5%A1%94%E7%A2%91_%E5%86%8A.pdf).
  - Yan Qinli Stele: facsimile of an early rubbing in two volumes, [volume 1](https://commons.wikimedia.org/wiki/File:SSID-12999612_%E5%88%9D%E6%8B%93%E9%A1%8F%E5%8B%A4%E7%A6%AE%E7%A2%91_%E4%B8%8A.pdf) (about 15.7 MB) and [volume 2](https://commons.wikimedia.org/wiki/File:SSID-12999613_%E5%88%9D%E6%8B%93%E9%A1%8F%E5%8B%A4%E7%A6%AE%E7%A2%91_%E4%B8%8B.pdf) (about 15.3 MB).
  - Thousand Character Classic: *Zhenshu Qianziwen*, Meiji 17 (1884) edition published by Sakata Teizō, [National Diet Library Digital Collections pid 853628](https://dl.ndl.go.jp/pid/853628), marked with the Public Domain Mark. The 22 page images (about 37 MB, downloaded through IIIF) are not committed; per-page SHA-256 values are recorded in `tools/yz_catalog.json`. The colophon attributes the calligraphy to Yan Zhenqing (dated Tianbao 5, carved by Shi Hua); the attribution is uncertain. All 1,000 characters were cut along the page grid (8 columns × 8 characters per spread), aligned character by character with the standard text, and checked by hand; 36 places where the print differs from the common text follow the print.
- Catalog: [`tools/yz_catalog.json`](../tools/yz_catalog.json) records each book's name, introduction, rubbing source, and chapters, and each character's chapter, simplified form, pinyin, structure, brush-technique focus, stele context, source, rubbing page, and pixel box. Every entry was checked by hand against the rubbing.
- Processing: crop by pixel box → per-character adaptive threshold (median background, 97th-percentile stroke level) → remove stone-flaw specks smaller than about 0.25% of a character cell → scale by the panel's cell size so relative character sizes are preserved → center on a 176×176 canvas → quantize to 4 bpp. Rubbing texture and damage are kept; nothing is retouched.
- Regenerate with `python3 tools/gen_yz_assets.py generate --src duobao=<Duobao PDF> --src qinli_1=<Qinli vol. 1> --src qinli_2=<Qinli vol. 2> --src qianzi=<directory of Thousand Character Classic page images p00.jpg…>` (requires pymupdf, Pillow, numpy, scipy); use `python3 tools/gen_yz_assets.py catalog` when only catalog text changes. `python3 tools/gen_yz_assets.py check` (part of `tools/validate.sh --static`) uses only the standard library to decode every glyph and compare the pack with the catalog and manifest; `preview --out <png>` writes a contact sheet for review.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
