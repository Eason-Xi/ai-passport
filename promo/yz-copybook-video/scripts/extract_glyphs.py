#!/usr/bin/env python3
"""从字帖分支取出 1412 个碑帖原字，生成视频用的字形素材。

输出（public/gen/glyphs/）：
  hero_<序号>.png  放大 4 倍的主角字（白字透明底，704×704），用于全屏砸字、闪切、集字标题；
  wall.png         33 列 × 44 行碑墙图集（每格 88 px，已着色），竖排、自右向左；
  glyphs.json      主角字清单、碑墙参数与各帖字数。

字形来源是 assets/images/yz_glyphs.bin（YZG1，176×176 4bpp），解码复用分支里
tools/gen_yz_assets.py 的 unpack_bin() / rle_decode()。原拓本每字约 150 px，176 px 已是
最高清的来源，所以放大只做平滑 + 边缘收紧 + 补一层固定种子的石花颗粒，不改动字形。

用法：.venv/bin/python scripts/extract_glyphs.py [--branch feature/yan-zhenqing-copybook]
依赖：numpy、Pillow
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

HERE = Path(__file__).resolve().parents[1]
OUT = HERE / "public" / "gen" / "glyphs"
SIZE = 176
HERO_SCALE = 4
WALL_COLS, WALL_ROWS, WALL_TILE = 33, 44, 88

# 闪切顺序：三本帖交替、繁简笔画疏密交替，越往后越快；最后一个字就是碑墙拉远的起点。
FLASH = ["大", "唐", "寶", "天", "書", "鳥", "龍", "心", "春", "秋", "日", "月", "明", "光", "道",
         "德", "雲", "雨", "山", "海", "金", "玉", "鳳", "華", "國", "家", "聖", "賢", "文", "章",
         "千", "年", "真", "法", "神", "靈", "清", "正", "王", "地"]
# 集字标题与开场「永」：都取自《多宝塔碑》（篇首「顏真卿書」、独体「永」）。
TITLE = {"yong": ("duobao", "永"), "yan": ("duobao", "顏"), "zhen": ("duobao", "真"),
         "qing": ("duobao", "卿")}


def git_show(branch: str, path: str) -> bytes:
    return subprocess.run(["git", "show", f"{branch}:{path}"], cwd=HERE, check=True,
                          capture_output=True).stdout


def load_codec(branch: str):
    src = git_show(branch, "tools/gen_yz_assets.py")
    tmp = Path(tempfile.mkdtemp()) / "gen_yz_assets.py"
    tmp.write_bytes(src)
    spec = importlib.util.spec_from_file_location("gen_yz_assets", tmp)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def decode_all(mod, blob: bytes) -> np.ndarray:
    blobs = mod.unpack_bin(blob)
    arr = np.zeros((len(blobs), SIZE, SIZE), dtype=np.float32)
    for i, b in enumerate(blobs):
        arr[i] = np.asarray(mod.rle_decode(b, SIZE * SIZE), dtype=np.float32).reshape(SIZE, SIZE) / 15.0
    return arr


def smoothstep(e0: float, e1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def hero(alpha: np.ndarray, seed: int) -> Image.Image:
    """176 px → 704 px：Lanczos 放大后收紧边缘，再叠一层石花（小坑）与颗粒。"""
    n = SIZE * HERO_SCALE
    up = Image.fromarray((alpha * 255).astype(np.uint8), "L").resize((n, n), Image.LANCZOS)
    up = up.filter(ImageFilter.GaussianBlur(0.9))
    a = np.asarray(up, dtype=np.float32) / 255.0
    a = smoothstep(0.12, 0.66, a)
    rng = np.random.default_rng(seed)
    # 颗粒：两层不同尺度的噪声轻微压暗笔画内部，模拟拓纸纹理。
    def noise(cells: int, blur: float) -> np.ndarray:
        raw = (rng.random((cells, cells)) * 255).astype(np.uint8)
        img = Image.fromarray(raw, "L").resize((n, n), Image.BICUBIC).filter(ImageFilter.GaussianBlur(blur))
        return np.asarray(img, dtype=np.float32) / 255.0
    grain = 0.6 * noise(n // 2, 0.6) + 0.4 * noise(n // 16, 3.0)
    # 石花：低频噪声取极值处形成大小不一、形状不规则的残损。
    pits = noise(n // 24, 1.5) * 0.65 + noise(n // 7, 1.0) * 0.35
    a = a * (0.78 + 0.22 * grain) * (1.0 - 0.9 * smoothstep(0.73, 0.83, pits))
    a = np.clip(a, 0, 1)
    rgba = np.zeros((n, n, 4), dtype=np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--branch", default="feature/yan-zhenqing-copybook")
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    mod = load_codec(args.branch)
    catalog = json.loads(git_show(args.branch, "tools/yz_catalog.json"))
    glyphs = decode_all(mod, git_show(args.branch, "assets/images/yz_glyphs.bin"))

    # 字形包按 多宝塔 → 勤礼碑 → 千字文 排列，下标与 catalog entries 一一对应。
    index: list[tuple[str, dict]] = []
    for book in catalog["books"]:
        for e in book["entries"]:
            index.append((book["key"], e))
    assert len(index) == len(glyphs) == 1412, (len(index), len(glyphs))

    def find(book: str | None, ch: str) -> int:
        for i, (b, e) in enumerate(index):
            if (book is None or b == book) and ch in (e["trad"], e["simp"]):
                return i
        raise SystemExit(f"找不到字：{book} {ch}")

    heroes = []
    for key, (book, ch) in TITLE.items():
        i = find(book, ch)
        hero(glyphs[i], seed=i).save(OUT / f"hero_{key}.png")
        heroes.append({"key": key, "char": ch, "book": book, "index": i, "file": f"gen/glyphs/hero_{key}.png"})
    flash = []
    for k, ch in enumerate(FLASH):
        i = find(None, ch)
        name = f"hero_f{k:02d}.png"
        hero(glyphs[i], seed=i).save(OUT / name)
        flash.append({"char": ch, "book": index[i][0], "index": i, "file": f"gen/glyphs/{name}"})

    # 碑墙：千字文在最右几列，竖排自右向左读（天地玄黃宇宙洪荒…），之后是多宝塔、勤礼碑。
    order = [i for i, (b, _) in enumerate(index) if b == "qianzi"]
    order += [i for i, (b, _) in enumerate(index) if b != "qianzi"]
    slots = WALL_COLS * WALL_ROWS
    order += order[: slots - len(order)]
    t = WALL_TILE
    atlas = np.zeros((WALL_ROWS * t, WALL_COLS * t), dtype=np.float32)
    pos = {}
    for s, gi in enumerate(order):
        col = WALL_COLS - 1 - s // WALL_ROWS
        row = s % WALL_ROWS
        small = Image.fromarray((glyphs[gi] * 255).astype(np.uint8), "L").resize((t - 8, t - 8), Image.LANCZOS)
        atlas[row * t + 4: row * t + t - 4, col * t + 4: col * t + t - 4] = np.asarray(small, dtype=np.float32) / 255
        pos.setdefault(gi, (col, row))
    # 不透明着色：与设备「拓本」底色一致（RGB565 量化后的 #181818 底、#efe7d6 字），
    # 格线 #2c2a28，渲染时无需滤镜，超大缩放也不会产生离屏图层。
    a = np.clip(atlas * 1.15, 0, 1)[..., None]
    bg = np.full(atlas.shape + (3,), 0x18, dtype=np.float32)
    grid = np.zeros(atlas.shape, dtype=bool)
    grid[::t, :] = grid[t - 1::t, :] = True
    grid[:, ::t] = grid[:, t - 1::t] = True
    bg[grid] = (0x2C, 0x2A, 0x28)
    ink = np.array([0xEF, 0xE7, 0xD6], dtype=np.float32)
    rgb = (bg * (1 - a) + ink * a).astype(np.uint8)
    Image.fromarray(rgb, "RGB").save(OUT / "wall.png", optimize=True)

    last = flash[-1]["index"]
    meta = {
        "heroes": heroes,
        "flash": flash,
        "wall": {"file": "gen/glyphs/wall.png", "cols": WALL_COLS, "rows": WALL_ROWS, "tile": t,
                 "focus": {"char": FLASH[-1], "col": pos[last][0], "row": pos[last][1]}},
        "books": [{"key": b["key"], "name": b["name"], "count": len(b["entries"])} for b in catalog["books"]],
        "total": len(index),
    }
    (OUT / "glyphs.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2))
    print(f"heroes={len(heroes)} flash={len(flash)} wall={WALL_COLS}x{WALL_ROWS} focus={meta['wall']['focus']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
