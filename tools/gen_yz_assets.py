#!/usr/bin/env python3
"""颜真卿字帖（《多宝塔碑》《颜勤礼碑》）资源生成 / 校验。

子命令：
  generate --pdf KEY=PATH ...  从拓本扫描裁切单字（KEY 为 tools/yz_catalog.json 的 sources 键），生成：
        assets/images/yz_glyphs.bin            4 bpp + 游程编码的字形包（固件 EMBED_FILES 嵌入）
        assets/images/yz_glyphs.manifest.json  来源、参数、每字哈希
        main/yz_catalog_data.c                 字帖、章节与字目文字表
        main/yz_catalog_size.h                 字帖 / 章节 / 字数常量
  catalog                     只重写 yz_catalog_data.c 与 yz_catalog_size.h（改了字目文字时用）
  check                       只用 Python 标准库：字目 C 文件是否最新、字形包结构与解码、
                              与 manifest 是否一致（已纳入 tools/validate.sh --static）
  preview --out <png>         把字形包解码成对照表（需要 Pillow），用于人工核对字与标注

字目来源 tools/yz_catalog.json 由人工逐字核对：每条记录写明拓本来源、页码、像素框与释文。
多本字帖的字依次排列在同一个字形包里；为了兼容旧存档，新字帖只能追加在末尾。
generate 依赖 pymupdf、Pillow、numpy、scipy，仅在重新裁切时需要；拓本 PDF 不入库，
其 SHA-256 记录在 yz_catalog.json 与 manifest 中，见 assets/README.md。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG_JSON = ROOT / "tools" / "yz_catalog.json"
GLYPH_BIN = ROOT / "assets" / "images" / "yz_glyphs.bin"
GLYPH_MANIFEST = ROOT / "assets" / "images" / "yz_glyphs.manifest.json"
CATALOG_C = ROOT / "main" / "yz_catalog_data.c"
CATALOG_SIZE_H = ROOT / "main" / "yz_catalog_size.h"

GLYPH_SIZE = 176          # 字形画布边长（像素），与 main/yz_glyph.h 的 YZ_GLYPH_SIZE 一致
MAGIC = b"YZG1"
HEADER = struct.Struct("<4sHHI")   # magic, count, size, data_offset
RECORD = struct.Struct("<II")      # offset (相对数据区), length

STRUCT_CODES = {"D": 0, "LR": 1, "TB": 2, "EN": 3}
FOCUS_CODES = {"H": 0, "S": 1, "P": 2, "N": 3, "D": 4, "G": 5, "Z": 6, "W": 7, "B": 8, "F": 9, "Y": 10}


# ---------------------------------------------------------------- 编解码（标准库）

def rle_encode(pixels: list[int]) -> bytes:
    """0x00–0x7F：b+1 个 0 像素；0x80–0xFF：(b&0x7F)+1 个字面像素，随后每字节两个 4 bit（高位在前）。"""
    out = bytearray()
    i, n = 0, len(pixels)
    while i < n:
        if pixels[i] == 0:
            j = i
            while j < n and pixels[j] == 0 and j - i < 128:
                j += 1
            out.append(j - i - 1)
            i = j
            continue
        j = i
        while j < n and j - i < 128:
            if pixels[j] == 0 and j + 2 < n and pixels[j + 1] == 0 and pixels[j + 2] == 0:
                break
            if pixels[j] == 0 and j + 2 >= n:
                break
            j += 1
        run = pixels[i:j]
        out.append(0x80 | (len(run) - 1))
        for k in range(0, len(run), 2):
            hi = run[k]
            lo = run[k + 1] if k + 1 < len(run) else 0
            out.append((hi << 4) | lo)
        i = j
    return bytes(out)


def rle_decode(data: bytes, count: int) -> list[int]:
    out: list[int] = []
    i = 0
    while i < len(data):
        b = data[i]
        i += 1
        if b < 0x80:
            out.extend([0] * (b + 1))
        else:
            n = (b & 0x7F) + 1
            for k in range(n):
                byte = data[i + k // 2]
                out.append(byte >> 4 if k % 2 == 0 else byte & 0x0F)
            i += (n + 1) // 2
        if len(out) > count:
            raise ValueError("游程超出画布")
    if len(out) != count:
        raise ValueError(f"解码像素数 {len(out)} != {count}")
    return out


def pack_bin(blobs: list[bytes]) -> bytes:
    table_size = HEADER.size + RECORD.size * len(blobs)
    out = bytearray(HEADER.pack(MAGIC, len(blobs), GLYPH_SIZE, table_size))
    offset = 0
    for blob in blobs:
        out += RECORD.pack(offset, len(blob))
        offset += len(blob)
    for blob in blobs:
        out += blob
    return bytes(out)


def unpack_bin(data: bytes) -> list[bytes]:
    magic, count, size, data_offset = HEADER.unpack_from(data, 0)
    if magic != MAGIC or size != GLYPH_SIZE:
        raise ValueError("字形包头无效")
    blobs = []
    for k in range(count):
        off, length = RECORD.unpack_from(data, HEADER.size + RECORD.size * k)
        start = data_offset + off
        if start + length > len(data):
            raise ValueError(f"第 {k} 个字形越界")
        blobs.append(data[start:start + length])
    return blobs


# ---------------------------------------------------------------- 字目 C 表

def c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def all_entries(cat: dict) -> list[dict]:
    return [e for book in cat["books"] for e in book["entries"]]


def render_catalog_c(cat: dict) -> str:
    lines = [
        "// main/yz_catalog_data.c —— 由 tools/gen_yz_assets.py 依据 tools/yz_catalog.json 生成，勿手改。",
        "// 字形包 assets/images/yz_glyphs.bin 中第 i 个字形对应 YZ_ENTRIES[i]。",
        '#include "yz_catalog.h"',
        "",
    ]
    books, chapters, entries = [], [], []
    for book in cat["books"]:
        first_chapter, first_entry = len(chapters), len(entries)
        emblem = None
        for c in book["chapters"]:
            idx = [i for i, e in enumerate(book["entries"]) if e["chapter"] == c["key"]]
            if not idx or idx != list(range(idx[0], idx[0] + len(idx))):
                raise SystemExit(f"{book['key']} 章节 {c['key']} 的字必须连续排列")
            chapters.append(f"    {{ {c_str(c['name'])}, {first_entry + idx[0]}, {len(idx)} }},")
        keys = {c["key"] for c in book["chapters"]}
        for e in book["entries"]:
            if e["chapter"] not in keys:
                raise SystemExit(f"{book['key']} 未知章节 {e['chapter']}")
            if emblem is None and e["trad"] == book["emblem"]:
                emblem = len(entries)
            entries.append(
                f"    {{ {c_str(e['simp'])}, {c_str(e['trad'])}, {c_str(e['pinyin'])}, {c_str(e['phrase'])}, "
                f"{c_str(e['source'])}, {STRUCT_CODES[e['struct']]}, {FOCUS_CODES[e['focus']]} }},")
        if emblem is None:
            raise SystemExit(f"{book['key']} 的题签字 {book['emblem']} 不在字目中")
        books.append(
            f"    {{ {c_str(book['name'])}, {c_str(book['name_v'])}, {c_str(book['era'])}, "
            f"{c_str(book['intro'])}, {c_str(book['source_text'])}, {emblem}, {first_chapter}, "
            f"{len(book['chapters'])}, {first_entry}, {len(book['entries'])} }},")
    lines += [f"const yz_book_info_t YZ_BOOKS[YZ_BOOK_COUNT] = {{", *books, "};", "",
              f"const yz_chapter_t YZ_CHAPTERS[YZ_CHAPTER_COUNT] = {{", *chapters, "};", "",
              f"const yz_entry_t YZ_ENTRIES[YZ_ENTRY_COUNT] = {{", *entries, "};", ""]
    return "\n".join(lines)


def render_size_h(cat: dict) -> str:
    books = cat["books"]
    return "\n".join([
        "// main/yz_catalog_size.h —— 由 tools/gen_yz_assets.py 生成，勿手改。",
        "#pragma once",
        "",
        f"#define YZ_BOOK_COUNT {len(books)}",
        f"#define YZ_CHAPTER_COUNT {sum(len(b['chapters']) for b in books)}",
        f"#define YZ_ENTRY_COUNT {sum(len(b['entries']) for b in books)}",
        f"#define YZ_BOOK_MAX_CHAPTERS {max(len(b['chapters']) for b in books)}",
        f"#define YZ_BOOK_MAX_ENTRIES {max(len(b['entries']) for b in books)}",
        "",
    ])


def load_catalog() -> dict:
    cat = json.loads(CATALOG_JSON.read_text(encoding="utf-8"))
    for book in cat["books"]:
        for e in book["entries"]:
            if e["trad"] not in e["phrase"]:
                raise SystemExit(f"{e['trad']} 不在其碑文语境「{e['phrase']}」中")
            if e["src"] not in cat["sources"]:
                raise SystemExit(f"{e['trad']} 的来源 {e['src']} 未登记")
    return cat


def write_catalog(cat: dict) -> None:
    CATALOG_C.write_text(render_catalog_c(cat), encoding="utf-8")
    CATALOG_SIZE_H.write_text(render_size_h(cat), encoding="utf-8")


# ---------------------------------------------------------------- 裁切（需第三方库）

def process_glyph(gray, entry):  # pragma: no cover - 依赖 numpy/scipy/PIL
    import numpy as np
    from PIL import Image
    from scipy import ndimage as ndi

    x0, y0, x1, y1 = entry["box"]
    cell = entry["cell"]
    pad = 8
    # gray 已由调用方四周补了 pad 个像素的边（见 page_gray），像素框可以贴着页边。
    crop = gray[y0:y1 + 2 * pad, x0:x1 + 2 * pad].astype(np.float32)
    h, w = crop.shape
    inside = np.zeros_like(crop, dtype=bool)
    inside[pad - 3:h - pad + 3, pad - 3:w - pad + 3] = True

    # 各页拓墨深浅不一，按单字自适应：背景取中位数，笔画亮度取 97 百分位。
    bg = float(np.median(crop[inside]))
    hi = float(np.percentile(crop[inside], 97))
    span = max(hi - bg, 1.0)
    smooth = ndi.gaussian_filter(crop, 1.5)
    ink = (smooth > bg + 0.33 * span) & inside
    lo, top = bg + 0.20 * span, bg + 0.85 * span
    alpha = np.clip((crop - lo) / (top - lo), 0.0, 1.0)

    # 去掉石花噪点：面积很小的连通域。框外的邻字已由 inside 屏蔽；
    # 册页剪裱留下的边线等个别残片通过收紧 tools/yz_catalog.json 中的像素框处理。
    lab, _ = ndi.label(ink)
    keep = np.zeros_like(ink)
    min_area = 0.0025 * cell * cell
    for k, sl in enumerate(ndi.find_objects(lab), start=1):
        comp = lab[sl] == k
        if int(comp.sum()) >= min_area:
            keep[sl] |= comp
    keep = ndi.binary_dilation(keep, structure=np.ones((5, 5), dtype=bool))
    alpha *= keep

    scale = GLYPH_SIZE * 0.97 / cell
    scale = min(scale, (GLYPH_SIZE - 6) / max(x1 - x0, y1 - y0))
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    img = Image.fromarray((alpha * 255).astype(np.uint8), "L").resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (GLYPH_SIZE, GLYPH_SIZE), 0)
    canvas.paste(img, ((GLYPH_SIZE - nw) // 2, (GLYPH_SIZE - nh) // 2))
    arr = np.asarray(canvas).astype(np.float32) / 255.0
    q = np.clip(np.rint(arr * 15), 0, 15).astype(np.uint8)
    return [int(v) for v in q.flatten()]


def page_gray(doc, page: int, pad: int = 8):  # pragma: no cover - 依赖 pymupdf/numpy
    import numpy as np
    import pymupdf

    xref = doc[page].get_images(full=True)[0][0]
    pix = pymupdf.Pixmap(doc, xref)
    if pix.n != 1:
        pix = pymupdf.Pixmap(pymupdf.csGRAY, pix)
    arr = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width)
    return np.pad(arr, pad, mode="edge")


def cmd_generate(args) -> int:  # pragma: no cover - 依赖第三方库
    import pymupdf

    cat = load_catalog()
    pdfs = dict(item.split("=", 1) for item in args.pdf)
    docs = {}
    for key, source in cat["sources"].items():
        if key not in pdfs:
            raise SystemExit(f"缺少拓本 {key}：--pdf {key}=<path>")
        data = Path(pdfs[key]).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != source["sha256"]:
            raise SystemExit(f"{key} 的 SHA-256 不符：{digest}")
        docs[key] = pymupdf.open(pdfs[key])
    pages: dict[tuple[str, int], object] = {}
    blobs, items = [], []
    for book in cat["books"]:
        for e in book["entries"]:
            key = (e["src"], e["page"])
            if key not in pages:
                pages[key] = page_gray(docs[e["src"]], e["page"])
            pixels = process_glyph(pages[key], e)
            blob = rle_encode(pixels)
            assert rle_decode(blob, GLYPH_SIZE * GLYPH_SIZE) == pixels
            blobs.append(blob)
            items.append({"book": book["key"], "trad": e["trad"], "bytes": len(blob),
                          "sha256": hashlib.sha256(blob).hexdigest()})
    data = pack_bin(blobs)
    GLYPH_BIN.write_bytes(data)
    manifest = {
        "sources": cat["sources"],
        "glyph_size": GLYPH_SIZE,
        "bits_per_pixel": 4,
        "encoding": "YZG1: header + (offset,len) table + RLE 4bpp (see tools/gen_yz_assets.py)",
        "count": len(blobs),
        "bin_bytes": len(data),
        "bin_sha256": hashlib.sha256(data).hexdigest(),
        "glyphs": items,
    }
    GLYPH_MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    write_catalog(cat)
    print(f"已生成 {len(blobs)} 个字形，共 {len(data)} 字节")
    return 0


def cmd_catalog(_args) -> int:
    write_catalog(load_catalog())
    print(f"已重写 {CATALOG_C.relative_to(ROOT)} 与 {CATALOG_SIZE_H.relative_to(ROOT)}")
    return 0


def cmd_check(_args) -> int:
    errors = []
    cat = load_catalog()
    if CATALOG_C.read_text(encoding="utf-8") != render_catalog_c(cat):
        errors.append("main/yz_catalog_data.c 与 tools/yz_catalog.json 不一致，运行 gen_yz_assets.py catalog")
    if CATALOG_SIZE_H.read_text(encoding="utf-8") != render_size_h(cat):
        errors.append("main/yz_catalog_size.h 与 tools/yz_catalog.json 不一致，运行 gen_yz_assets.py catalog")
    entries = all_entries(cat)
    data = GLYPH_BIN.read_bytes()
    manifest = json.loads(GLYPH_MANIFEST.read_text(encoding="utf-8"))
    if hashlib.sha256(data).hexdigest() != manifest["bin_sha256"]:
        errors.append("yz_glyphs.bin 与 manifest 哈希不符")
    if manifest["sources"] != cat["sources"]:
        errors.append("manifest 记录的拓本来源与 yz_catalog.json 不一致")
    blobs = unpack_bin(data)
    if len(blobs) != len(entries) or len(blobs) != manifest["count"]:
        errors.append(f"字形数 {len(blobs)} 与字目 {len(entries)} / manifest {manifest['count']} 不符")
    for k, (blob, e) in enumerate(zip(blobs, entries)):
        item = manifest["glyphs"][k]
        if item["trad"] != e["trad"]:
            errors.append(f"第 {k} 个字形是「{item['trad']}」，字目却是「{e['trad']}」")
        try:
            px = rle_decode(blob, GLYPH_SIZE * GLYPH_SIZE)
        except ValueError as exc:
            errors.append(f"第 {k} 个字形解码失败：{exc}")
            continue
        if max(px) == 0:
            errors.append(f"第 {k} 个字形「{e['trad']}」是空白")
    for msg in errors:
        print("ERROR:", msg, file=sys.stderr)
    if errors:
        return 1
    names = "、".join(f"{b['name']} {len(b['entries'])} 字" for b in cat["books"])
    print(f"字帖资源检查通过：{names}，{len(data)} 字节")
    return 0


def cmd_preview(args) -> int:  # pragma: no cover - 依赖 Pillow
    from PIL import Image, ImageDraw, ImageFont

    cat = load_catalog()
    entries = all_entries(cat)
    if args.book:
        first = 0
        for book in cat["books"]:
            if book["key"] == args.book:
                break
            first += len(book["entries"])
        else:
            raise SystemExit(f"未知字帖 {args.book}")
        span = range(first, first + len(book["entries"]))
    else:
        span = range(len(entries))
    blobs = unpack_bin(GLYPH_BIN.read_bytes())
    cols, cw, ch = 12, 100, 128
    rows = (len(span) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw, rows * ch), (238, 232, 218))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(args.font, 16) if args.font else ImageFont.load_default()
    for n, k in enumerate(span):
        e = entries[k]
        px = bytes(v * 17 for v in rle_decode(blobs[k], GLYPH_SIZE * GLYPH_SIZE))
        g = Image.frombytes("L", (GLYPH_SIZE, GLYPH_SIZE), px).resize((92, 92), Image.LANCZOS)
        tile = Image.new("RGB", (92, 92), (30, 29, 27))
        tile.paste(Image.new("RGB", (92, 92), (240, 236, 226)), (0, 0), g)
        x, y = (n % cols) * cw + 4, (n // cols) * ch + 4
        sheet.paste(tile, (x, y))
        draw.text((x, y + 94), f"{k} {e['trad']}{e['simp']} {e['pinyin']}", fill=(160, 30, 20), font=font)
    sheet.save(args.out)
    print(f"已写出 {args.out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--pdf", action="append", required=True, metavar="KEY=PATH",
                   help="拓本 PDF，可重复：duobao=… qinli_1=… qinli_2=…")
    sub.add_parser("catalog")
    sub.add_parser("check")
    p = sub.add_parser("preview")
    p.add_argument("--out", required=True)
    p.add_argument("--book", help="只输出某一本字帖（yz_catalog.json 中的 key）")
    p.add_argument("--font", help="用于标注的 TTF/OTF 字体（需含汉字）")
    args = parser.parse_args()
    return {"generate": cmd_generate, "catalog": cmd_catalog, "check": cmd_check,
            "preview": cmd_preview}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
