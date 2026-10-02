#!/usr/bin/env python3
"""颜真卿《多宝塔碑》字帖资源生成 / 校验。

子命令：
  generate --pdf <拓本 PDF>   从拓本扫描裁切单字，生成：
        assets/images/yz_glyphs.bin            4 bpp + 游程编码的字形包（固件 EMBED_FILES 嵌入）
        assets/images/yz_glyphs.manifest.json  来源、参数、每字哈希
        main/yz_catalog_data.c                 字目文字表（简体、拼音、碑文语境、结构、笔法）
  catalog                     只重写 main/yz_catalog_data.c（改了 tools/yz_catalog.json 的文字时用）
  check                       只用 Python 标准库：字目 C 文件是否最新、字形包结构与解码、
                              与 manifest 是否一致（已纳入 tools/validate.sh --static）
  preview --out <png>         把字形包解码成对照表（需要 Pillow），用于人工核对字与标注

字目来源 tools/yz_catalog.json 由人工逐字核对：每条记录写明拓本页码、像素框与释文。
generate 依赖 pymupdf、Pillow、numpy、scipy，仅在重新裁切时需要；拓本 PDF 不入库，
其 SHA-256 记录在 manifest 中，见 assets/README.md。
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

SOURCE_URL = ("https://commons.wikimedia.org/wiki/File:NPM-%E6%95%85%E5%B8%96000019_"
              "%E5%AE%8B%E6%8B%93%E5%A4%9A%E5%AF%B6%E4%BD%9B%E5%A1%94%E7%A2%91_%E5%86%8A.pdf")
SOURCE_SHA256 = "3b231dbb95ef0ac871e07a0162a0c8587f16ef9d3cbee26df98483e26dd02069"

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
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_catalog_c(cat: dict) -> str:
    chapters = cat["chapters"]
    entries = cat["entries"]
    keys = [c["key"] for c in chapters]
    lines = [
        "// main/yz_catalog_data.c —— 由 tools/gen_yz_assets.py 依据 tools/yz_catalog.json 生成，勿手改。",
        "// 字形包 assets/images/yz_glyphs.bin 中第 i 个字形对应 YZ_ENTRIES[i]。",
        '#include "yz_catalog.h"',
        "",
        f"const yz_chapter_t YZ_CHAPTERS[{len(chapters)}] = {{",
    ]
    for c in chapters:
        idx = [i for i, e in enumerate(entries) if e["chapter"] == c["key"]]
        if not idx or idx != list(range(idx[0], idx[0] + len(idx))):
            raise SystemExit(f"章节 {c['key']} 的字必须连续排列")
        lines.append(f"    {{ {c_str(c['name'])}, {idx[0]}, {len(idx)} }},")
    lines += ["};", "", f"const yz_entry_t YZ_ENTRIES[{len(entries)}] = {{"]
    for e in entries:
        if e["chapter"] not in keys:
            raise SystemExit(f"未知章节 {e['chapter']}")
        lines.append(
            f"    {{ {c_str(e['simp'])}, {c_str(e['trad'])}, {c_str(e['pinyin'])}, {c_str(e['phrase'])}, "
            f"{STRUCT_CODES[e['struct']]}, {FOCUS_CODES[e['focus']]}, {e['page'] + 1}, "
            f"{0 if e['side'] == '右' else 1} }},")
    lines += ["};", ""]
    return "\n".join(lines)


def load_catalog() -> dict:
    cat = json.loads(CATALOG_JSON.read_text(encoding="utf-8"))
    for e in cat["entries"]:
        if e["trad"] not in e["phrase"]:
            raise SystemExit(f"{e['trad']} 不在其碑文语境「{e['phrase']}」中")
    return cat


# ---------------------------------------------------------------- 裁切（需第三方库）

def process_glyph(gray, entry):  # pragma: no cover - 依赖 numpy/scipy/PIL
    import numpy as np
    from PIL import Image
    from scipy import ndimage as ndi

    x0, y0, x1, y1 = entry["box"]
    cell = entry["cell"]
    pad = 8
    crop = gray[y0 - pad:y1 + pad, x0 - pad:x1 + pad].astype(np.float32)
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


def cmd_generate(args) -> int:  # pragma: no cover - 依赖第三方库
    import pymupdf
    import numpy as np

    pdf = Path(args.pdf)
    digest = hashlib.sha256(pdf.read_bytes()).hexdigest()
    if digest != SOURCE_SHA256:
        raise SystemExit(f"PDF SHA-256 不符：{digest}")
    cat = load_catalog()
    doc = pymupdf.open(str(pdf))
    pages: dict[int, object] = {}
    blobs, items = [], []
    for e in cat["entries"]:
        p = e["page"]
        if p not in pages:
            xref = doc[p].get_images(full=True)[0][0]
            pix = pymupdf.Pixmap(doc, xref)
            if pix.n != 1:
                pix = pymupdf.Pixmap(pymupdf.csGRAY, pix)
            pages[p] = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width)
        pixels = process_glyph(pages[p], e)
        blob = rle_encode(pixels)
        assert rle_decode(blob, GLYPH_SIZE * GLYPH_SIZE) == pixels
        blobs.append(blob)
        items.append({"trad": e["trad"], "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()})
    data = pack_bin(blobs)
    GLYPH_BIN.write_bytes(data)
    manifest = {
        "source": {
            "title": "宋拓多寶佛塔碑 冊（台北故宫博物院 故帖000019）",
            "url": SOURCE_URL,
            "license": "Public domain (Wikimedia Commons)",
            "sha256": digest,
        },
        "glyph_size": GLYPH_SIZE,
        "bits_per_pixel": 4,
        "encoding": "YZG1: header + (offset,len) table + RLE 4bpp (see tools/gen_yz_assets.py)",
        "count": len(blobs),
        "bin_bytes": len(data),
        "bin_sha256": hashlib.sha256(data).hexdigest(),
        "glyphs": items,
    }
    GLYPH_MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    CATALOG_C.write_text(render_catalog_c(cat), encoding="utf-8")
    print(f"已生成 {len(blobs)} 个字形，共 {len(data)} 字节")
    return 0


def cmd_catalog(_args) -> int:
    CATALOG_C.write_text(render_catalog_c(load_catalog()), encoding="utf-8")
    print(f"已重写 {CATALOG_C.relative_to(ROOT)}")
    return 0


def cmd_check(_args) -> int:
    errors = []
    cat = load_catalog()
    if CATALOG_C.read_text(encoding="utf-8") != render_catalog_c(cat):
        errors.append("main/yz_catalog_data.c 与 tools/yz_catalog.json 不一致，运行 gen_yz_assets.py catalog")
    data = GLYPH_BIN.read_bytes()
    manifest = json.loads(GLYPH_MANIFEST.read_text(encoding="utf-8"))
    if hashlib.sha256(data).hexdigest() != manifest["bin_sha256"]:
        errors.append("yz_glyphs.bin 与 manifest 哈希不符")
    blobs = unpack_bin(data)
    if len(blobs) != len(cat["entries"]) or len(blobs) != manifest["count"]:
        errors.append(f"字形数 {len(blobs)} 与字目 {len(cat['entries'])} / manifest {manifest['count']} 不符")
    for k, (blob, e) in enumerate(zip(blobs, cat["entries"])):
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
    print(f"字帖资源检查通过：{len(blobs)} 字，{len(data)} 字节")
    return 0


def cmd_preview(args) -> int:  # pragma: no cover - 依赖 Pillow
    from PIL import Image, ImageDraw, ImageFont

    cat = load_catalog()
    blobs = unpack_bin(GLYPH_BIN.read_bytes())
    cols, cw, ch = 12, 100, 128
    rows = (len(blobs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw, rows * ch), (238, 232, 218))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(args.font, 16) if args.font else ImageFont.load_default()
    for k, (blob, e) in enumerate(zip(blobs, cat["entries"])):
        px = bytes(v * 17 for v in rle_decode(blob, GLYPH_SIZE * GLYPH_SIZE))
        g = Image.frombytes("L", (GLYPH_SIZE, GLYPH_SIZE), px).resize((92, 92), Image.LANCZOS)
        tile = Image.new("RGB", (92, 92), (30, 29, 27))
        tile.paste(Image.new("RGB", (92, 92), (240, 236, 226)), (0, 0), g)
        x, y = (k % cols) * cw + 4, (k // cols) * ch + 4
        sheet.paste(tile, (x, y))
        draw.text((x, y + 94), f"{k} {e['trad']}{e['simp']} {e['pinyin']}", fill=(160, 30, 20), font=font)
    sheet.save(args.out)
    print(f"已写出 {args.out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--pdf", required=True)
    sub.add_parser("catalog")
    sub.add_parser("check")
    p = sub.add_parser("preview")
    p.add_argument("--out", required=True)
    p.add_argument("--font", help="用于标注的 TTF/OTF 字体（需含汉字）")
    args = parser.parse_args()
    return {"generate": cmd_generate, "catalog": cmd_catalog, "check": cmd_check,
            "preview": cmd_preview}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
