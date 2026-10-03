#!/usr/bin/env python3
"""颜真卿《多宝塔碑》字帖资源生成 / 校验。

子命令：
  generate --src DIR   从高清剪裱本逐页图像（DIR/01.jpg … 42.jpg）裁切全碑每一个字，生成：
        assets/images/yz_glyphs.bin            4 bpp + 游程编码的字形包（固件 EMBED_FILES 嵌入）
        assets/images/yz_glyphs.manifest.json  来源、参数、每字哈希
        main/yz_catalog_data.c                 字帖、分卷与字目文字表
        main/yz_catalog_size.h                 分卷 / 字数常量
  catalog                     只重写 yz_catalog_data.c 与 yz_catalog_size.h（改了字目文字时用）
  check                       只用 Python 标准库：字目 C 文件是否最新、字形包结构与解码、
                              与 manifest 是否一致（已纳入 tools/validate.sh --static）
  preview --out <png>         把字形包解码成对照表（需要 Pillow），用于人工核对字与释文

字目来源 tools/yz_catalog.json 逐字记录释文、剪裱本页码与像素框，均已对照原拓逐字核对。
字序即碑文顺序：前几卷按碑文分段（每卷是字目中连续的一段），分类卷（数目、独体、左右、上下、
包围）引用同一批字形，每个不同的字只收一次。
generate 依赖 Pillow、numpy、scipy，仅在重新裁切时需要；剪裱本图像不入库，
逐页 SHA-256 记录在 yz_catalog.json 与 manifest 中，见 assets/README.md。
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

# 字形以接近原图的分辨率存储（STORE_SIZE），固件解码时双线性放大到 DISPLAY_SIZE，
# 与 main/yz_glyph.h 的 YZ_GLYPH_SIZE 一致。剪裱本每字约 100–130 像素，128 已保留全部细节。
STORE_SIZE = 128
DISPLAY_SIZE = 176
MAGIC = b"YZG1"
HEADER = struct.Struct("<4sHHI")   # magic, count, 存储边长, data_offset
RECORD = struct.Struct("<II")      # offset (相对数据区), length

STRUCT_CODES = {"D": 0, "LR": 1, "TB": 2, "EN": 3}
FOCUS_CODES = {"H": 0, "S": 1, "P": 2, "N": 3, "D": 4, "G": 5, "Z": 6, "W": 7, "B": 8, "F": 9, "Y": 10}
CAT_OF_STRUCT = {"D": "single", "LR": "lr", "TB": "tb", "EN": "en"}


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


def pack_bin(blobs: list[bytes], size: int = STORE_SIZE) -> bytes:
    table_size = HEADER.size + RECORD.size * len(blobs)
    out = bytearray(HEADER.pack(MAGIC, len(blobs), size, table_size))
    offset = 0
    for blob in blobs:
        out += RECORD.pack(offset, len(blob))
        offset += len(blob)
    for blob in blobs:
        out += blob
    return bytes(out)


def unpack_bin(data: bytes) -> list[bytes]:
    magic, count, size, data_offset = HEADER.unpack_from(data, 0)
    if magic != MAGIC or size != STORE_SIZE:
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


def chapter_items(book: dict) -> list[list[int]]:
    """每卷包含的字（字目下标）。

    碑文卷：range 给出字目中连续的一段，各卷首尾相接、覆盖全碑。
    分类卷：按每字的 cat 字段归类，每个不同的字只收一次——取碑文中第一次出现、且未标
    damaged（残损）的那一处；全都残损时取第一处。数目卷可用 order 指定排列顺序。
    """
    entries = book["entries"]
    out = []
    expect = 0
    for c in book["chapters"]:
        if "range" in c:
            a, b = c["range"]
            if a != expect or b <= a:
                raise SystemExit(f"碑文卷 {c['key']} 的范围 {c['range']} 没有与上一卷首尾相接")
            expect = b
            out.append(list(range(a, b)))
            continue
        first: dict[str, int] = {}
        for i, e in enumerate(entries):
            if e["cat"] != c["key"]:
                continue
            k = first.get(e["trad"])
            if k is None or (entries[k].get("damaged") and not e.get("damaged")):
                first[e["trad"]] = i
        idx = sorted(first.values())
        if "order" in c:
            idx.sort(key=lambda i: c["order"].index(entries[i]["trad"]))
        if not idx:
            raise SystemExit(f"分类卷 {c['key']} 没有字")
        out.append(idx)
    if expect != len(entries):
        raise SystemExit(f"碑文卷只覆盖到第 {expect} 字，字目共 {len(entries)} 字")
    return out


def render_catalog_c(cat: dict) -> str:
    book = cat["book"]
    lines = [
        "// main/yz_catalog_data.c —— 由 tools/gen_yz_assets.py 依据 tools/yz_catalog.json 生成，勿手改。",
        "// 字形包 assets/images/yz_glyphs.bin 中第 i 个字形对应 YZ_ENTRIES[i]（碑文顺序）。",
        '#include "yz_catalog.h"',
        "",
        "const yz_book_info_t YZ_BOOK = {",
        f"    {c_str(book['name'])}, {c_str(book['name_v'])}, {c_str(book['era'])}, {c_str(book['author'])},",
        f"    {c_str(book['phrase_tag'])},",
        f"    {c_str(book['intro'])},",
        f"    {c_str(book['source_text'])},",
        "};",
        "",
    ]
    chapters, items = [], []
    for c, idx in zip(book["chapters"], chapter_items(book)):
        chapters.append(f"    {{ {c_str(c['name'])}, {len(items)}, {len(idx)}, {c.get('cols', 5)}, "
                        f"{1 if 'range' in c else 0} }},")
        items += idx
    entries = []
    for e in book["entries"]:
        entries.append(
            f"    {{ {c_str(e['simp'])}, {c_str(e['trad'])}, {c_str(e['pinyin'])}, {c_str(e['phrase'])}, "
            f"{c_str(e['source'])}, {STRUCT_CODES[e['struct']]}, {FOCUS_CODES[e['focus']]} }},")
    item_rows = [", ".join(str(v) for v in items[k:k + 16]) for k in range(0, len(items), 16)]
    lines += ["const yz_chapter_t YZ_CHAPTERS[YZ_CHAPTER_COUNT] = {", *chapters, "};", "",
              "const uint16_t YZ_CHAPTER_ITEMS[YZ_CHAPTER_ITEM_COUNT] = {",
              *[f"    {row}," for row in item_rows], "};", "",
              "const yz_entry_t YZ_ENTRIES[YZ_ENTRY_COUNT] = {", *entries, "};", ""]
    return "\n".join(lines)


def render_size_h(cat: dict) -> str:
    book = cat["book"]
    items = chapter_items(book)
    return "\n".join([
        "// main/yz_catalog_size.h —— 由 tools/gen_yz_assets.py 生成，勿手改。",
        "#pragma once",
        "",
        f"#define YZ_CHAPTER_COUNT {len(book['chapters'])}",
        f"#define YZ_TEXT_CHAPTER_COUNT {sum(1 for c in book['chapters'] if 'range' in c)}",
        f"#define YZ_CHAPTER_ITEM_COUNT {sum(len(i) for i in items)}",
        f"#define YZ_CHAPTER_MAX_ITEMS {max(len(i) for i in items)}",
        f"#define YZ_ENTRY_COUNT {len(book['entries'])}",
        f"#define YZ_CATALOG_MAX_COLS {max(c.get('cols', 5) for c in book['chapters'])}",
        "",
    ])


def load_catalog() -> dict:
    cat = json.loads(CATALOG_JSON.read_text(encoding="utf-8"))
    book = cat["book"]
    keys = {c["key"] for c in book["chapters"]}
    for k, e in enumerate(book["entries"]):
        if e["trad"] not in e["phrase"]:
            raise SystemExit(f"第 {k} 字 {e['trad']} 不在其碑文语境「{e['phrase']}」中")
        if e["cat"] not in keys or e["chapter"] not in keys:
            raise SystemExit(f"第 {k} 字 {e['trad']} 的分卷 / 分类未登记")
        if e["cat"] != "num" and e["cat"] != CAT_OF_STRUCT[e["struct"]]:
            raise SystemExit(f"第 {k} 字 {e['trad']} 的分类 {e['cat']} 与结构 {e['struct']} 不符")
        if e["src"] not in cat["sources"]:
            raise SystemExit(f"第 {k} 字 {e['trad']} 的来源 {e['src']} 未登记")
    return cat


def write_catalog(cat: dict) -> None:
    CATALOG_C.write_text(render_catalog_c(cat), encoding="utf-8")
    CATALOG_SIZE_H.write_text(render_size_h(cat), encoding="utf-8")


# ---------------------------------------------------------------- 裁切（需第三方库）

PAD = 8


def red_mask(rgb):  # pragma: no cover - 依赖 numpy
    """朱印（收藏印、补纸上的印）：色相偏红、饱和度高于石面。字是米黄色，色相约 0.09。"""
    import numpy as np

    f = rgb.astype(np.float32)
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    sat = (mx - mn) / (mx + 1e-6)
    hue_red = (r >= mx) & ((g - b) / (mx - mn + 1e-6) < 0.36)     # 色相 < 约 0.06
    return hue_red & (sat > 0.25) & (mx > 60)


def process_glyph(rgb, entry, cell):  # pragma: no cover - 依赖 numpy/scipy/PIL
    import numpy as np
    from PIL import Image
    from scipy import ndimage as ndi

    x0, y0, x1, y1 = entry["box"]
    # rgb 已由调用方四周补了 PAD 个像素的边（见 page_rgb），像素框可以贴着页边。
    m = PAD + 12                                        # 统计背景用的外扩边
    X0, Y0 = max(0, x0 + PAD - m), max(0, y0 + PAD - m)
    X1, Y1 = min(rgb.shape[1], x1 + PAD + m), min(rgb.shape[0], y1 + PAD + m)
    sub = rgb[Y0:Y1, X0:X1].astype(np.float32)
    lum = 0.299 * sub[..., 0] + 0.587 * sub[..., 1] + 0.114 * sub[..., 2]
    h, w = lum.shape
    bx0, by0 = x0 + PAD - X0, y0 + PAD - Y0
    bx1, by1 = bx0 + (x1 - x0), by0 + (y1 - y0)

    # 不算笔画的区域：朱印、人工标注的邻字残片与裱补纸（erase，页面坐标）。
    void = ndi.binary_dilation(red_mask(sub), iterations=2)
    for ex0, ey0, ex1, ey1 in entry.get("erase", []):
        void[max(0, ey0 + PAD - Y0):max(0, ey1 + PAD - Y0), max(0, ex0 + PAD - X0):max(0, ex1 + PAD - X0)] = True
    inside = np.zeros((h, w), dtype=bool)
    inside[max(0, by0 - 3):by1 + 3, max(0, bx0 - 3):bx1 + 3] = True
    inside &= ~void

    # 各页拓墨深浅不一，按单字自适应：石面取外扩区的 40 百分位，笔画亮度取框内 97 百分位。
    stats = ~void
    bg = float(np.percentile(lum[stats], 40))
    hi = float(np.percentile(lum[inside], 97))
    span = max(hi - bg, 1.0)
    smooth = ndi.gaussian_filter(lum, 1.5)
    ink = (smooth > bg + 0.33 * span) & inside
    lo, top = bg + 0.20 * span, bg + 0.85 * span
    alpha = np.clip((lum - lo) / (top - lo), 0.0, 1.0)

    # 去掉石花噪点（面积很小的连通域）；框外的邻字已由 inside 屏蔽。
    lab, _ = ndi.label(ink)
    keep = np.zeros_like(ink)
    min_area = 0.0025 * cell * cell
    for k, sl in enumerate(ndi.find_objects(lab), start=1):
        comp = lab[sl] == k
        if int(comp.sum()) >= min_area * (0.3 if entry.get("note") else 1.0):
            keep[sl] |= comp
    keep = ndi.binary_dilation(keep, structure=np.ones((5, 5), dtype=bool)) & ~void
    alpha *= keep

    # 按统一字格缩放（保留字与字的相对大小），以像素框中心居中。
    scale = min(STORE_SIZE * 0.97 / cell, (STORE_SIZE - 6) / max(x1 - x0, y1 - y0))
    cx, cy = (bx0 + bx1) / 2.0, (by0 + by1) / 2.0
    img = Image.fromarray((alpha * 255).astype(np.uint8), "L")
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    img = img.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("L", (STORE_SIZE, STORE_SIZE), 0)
    canvas.paste(img, (round(STORE_SIZE / 2 - cx * scale), round(STORE_SIZE / 2 - cy * scale)))
    arr = np.asarray(canvas).astype(np.float32) / 255.0
    q = np.clip(np.rint(arr * 15), 0, 15).astype(np.uint8)
    return [int(v) for v in q.flatten()]


def page_rgb(path: Path, page: int):  # pragma: no cover
    import numpy as np
    from PIL import Image

    arr = np.asarray(Image.open(path / f"{page:02d}.jpg").convert("RGB"))
    return np.pad(arr, ((PAD, PAD), (PAD, PAD), (0, 0)), mode="edge")


def verify_source(key: str, source: dict, path: Path) -> None:  # pragma: no cover
    for name, digest in source["pages"].items():
        got = hashlib.sha256((path / f"{name}.jpg").read_bytes()).hexdigest()
        if got != digest:
            raise SystemExit(f"{key}/{name}.jpg 的 SHA-256 不符：{got}")


def cmd_generate(args) -> int:  # pragma: no cover - 依赖第三方库
    cat = load_catalog()
    (key, source), = cat["sources"].items()
    path = Path(args.src)
    verify_source(key, source, path)
    pages: dict[int, object] = {}
    blobs, items = [], []
    for e in cat["book"]["entries"]:
        if e["page"] not in pages:
            pages[e["page"]] = page_rgb(path, e["page"])
        pixels = process_glyph(pages[e["page"]], e, e.get("cell", source["cell"]))
        blob = rle_encode(pixels)
        assert rle_decode(blob, STORE_SIZE * STORE_SIZE) == pixels
        blobs.append(blob)
        items.append({"trad": e["trad"], "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()})
    data = pack_bin(blobs)
    GLYPH_BIN.write_bytes(data)
    manifest = {
        "sources": cat["sources"],
        "store_size": STORE_SIZE,
        "display_size": DISPLAY_SIZE,
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
    entries = cat["book"]["entries"]
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
            px = rle_decode(blob, STORE_SIZE * STORE_SIZE)
        except ValueError as exc:
            errors.append(f"第 {k} 个字形解码失败：{exc}")
            continue
        if max(px) == 0:
            errors.append(f"第 {k} 个字形「{e['trad']}」是空白")
    for msg in errors:
        print("ERROR:", msg, file=sys.stderr)
    if errors:
        return 1
    distinct = len({e["trad"] for e in entries})
    print(f"字帖资源检查通过：{cat['book']['name']} {len(entries)} 字（{distinct} 个不同的字），{len(data)} 字节")
    return 0


def cmd_preview(args) -> int:  # pragma: no cover - 依赖 Pillow
    from PIL import Image, ImageDraw, ImageFont

    entries = load_catalog()["book"]["entries"]
    a, b = args.range or (0, len(entries))
    span = range(max(0, a), min(len(entries), b))
    blobs = unpack_bin(GLYPH_BIN.read_bytes())
    cols, cw, ch = 12, 100, 128
    rows = (len(span) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw, rows * ch), (238, 232, 218))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(args.font, 16) if args.font else ImageFont.load_default()
    for n, k in enumerate(span):
        e = entries[k]
        px = bytes(v * 17 for v in rle_decode(blobs[k], STORE_SIZE * STORE_SIZE))
        g = Image.frombytes("L", (STORE_SIZE, STORE_SIZE), px).resize((92, 92), Image.LANCZOS)
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
    g.add_argument("--src", required=True, metavar="DIR", help="剪裱本逐页图像目录（01.jpg … 42.jpg）")
    sub.add_parser("catalog")
    sub.add_parser("check")
    p = sub.add_parser("preview")
    p.add_argument("--out", required=True)
    p.add_argument("--range", type=int, nargs=2, metavar=("FROM", "TO"), help="只输出字目下标 [FROM, TO)")
    p.add_argument("--font", help="用于标注的 TTF/OTF 字体（需含汉字）")
    args = parser.parse_args()
    return {"generate": cmd_generate, "catalog": cmd_catalog, "check": cmd_check,
            "preview": cmd_preview}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
