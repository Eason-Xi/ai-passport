#!/usr/bin/env python3
"""在主机上用真实的 LVGL + ASMR 声景播放器界面代码渲染各页面，输出 PNG 与 LVGL 内存池占用。

开发辅助，不进校验门，也不参与固件构建。它能发现：
  * 缺字形（占位框）、文字裁切 / 重叠、版面越界；
  * 每个页面的 LVGL 内置内存池峰值（固件为 48 KB）。
它不能替代真机：色彩、刷新与按键手感只有上板才知道。

依赖：C 编译器、Python 3 标准库；LVGL 源码取自 idf.py 下载到 managed_components/ 的那份
（版本由 dependencies.lock 锁定）。

用法（仓库根目录）：
  python3 tools/render_asmr_preview.py [--out build/preview] [--scale 2] [--mem-kb 48]
峰值超过内存池 75%（--budget）时失败：池耗尽在板上表现为卡死 / 白屏。
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LVGL_DIR = ROOT / "managed_components" / "lvgl__lvgl"
PREVIEW_DIR = ROOT / "tools" / "asmr_preview"
APP_SOURCES = ["as_dsp.c", "as_cfg.c", "as_crc32.c", "as_breath.c", "as_model.c", "as_fonts.c"]
SCREEN_W, SCREEN_H, CORNER_RADIUS = 240, 320, 30


def compile_flags(mem_kb: int) -> list[str]:
    return ["-std=gnu11", "-O1", "-g0", "-w",
            f"-DLV_CONF_PATH=\"{PREVIEW_DIR / 'lv_conf.h'}\"", f"-DAS_PREVIEW_MEM_KB={mem_kb}",
            f"-I{LVGL_DIR}", f"-I{LVGL_DIR / 'src'}", f"-I{ROOT / 'main'}", f"-I{PREVIEW_DIR}"]


def compile_one(source: Path, obj: Path, flags: list[str], cache: bool) -> None:
    # 只缓存 LVGL 本身：应用代码依赖的头文件很多，按 .c 时间戳缓存会得到过期结果。
    if cache and obj.exists() and obj.stat().st_mtime >= source.stat().st_mtime:
        return
    obj.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["cc", *flags, "-c", str(source), "-o", str(obj)], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"ERROR compiling {source}:\n{result.stderr}")


def build(out: Path, mem_kb: int) -> Path:
    if not LVGL_DIR.is_dir():
        raise SystemExit("ERROR: managed_components/lvgl__lvgl not found; run idf.py build "
                         "or ./tools/validate.sh --firmware once to download the pinned LVGL")
    flags = compile_flags(mem_kb)
    conf_hash = hashlib.sha256((PREVIEW_DIR / "lv_conf.h").read_bytes()).hexdigest()[:8]
    obj_root = out / f"obj{mem_kb}-{conf_hash}"
    lvgl_sources = sorted((LVGL_DIR / "src").rglob("*.c"))
    app_sources = [ROOT / "main" / n for n in APP_SOURCES]
    app_sources += sorted((ROOT / "main").glob("as_ui*.c"))
    app_sources += sorted((ROOT / "assets" / "fonts").glob("as_*.c"))
    app_sources.append(PREVIEW_DIR / "preview_main.c")

    jobs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for source in lvgl_sources + app_sources:
            obj = obj_root / (str(source.relative_to(ROOT)).replace("/", "__") + ".o")
            these = flags
            if source.parent == ROOT / "main":
                these = [f for f in flags if f != "-w"] + ["-Wall", "-Wextra", "-Werror"]
            jobs.append(pool.submit(compile_one, source, obj, these, LVGL_DIR in source.parents))
        for job in jobs:
            job.result()
    binary = out / f"preview{mem_kb}"
    objs = [str(p) for p in obj_root.rglob("*.o")]
    link = subprocess.run(["cc", *objs, "-o", str(binary), "-lm"], capture_output=True, text=True)
    if link.returncode != 0:
        raise SystemExit(f"ERROR linking:\n{link.stderr}")
    return binary


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    assert match, path
    return int(match.group(1)), int(match.group(2)), data[match.end():]


def mask_corners(rgb: bytearray, width: int, height: int, radius: int) -> None:
    """复现 BSP 的屏幕圆角遮挡（BSP_LVGL_SCREEN_RADIUS），看内容会不会被角落吃掉。"""
    for cy, cx in ((radius, radius), (radius, width - radius), (height - radius, radius),
                   (height - radius, width - radius)):
        for y in range(max(0, cy - radius), min(height, cy + radius)):
            for x in range(max(0, cx - radius), min(width, cx + radius)):
                corner_y = y < radius or y >= height - radius
                corner_x = x < radius or x >= width - radius
                if corner_x and corner_y and (x - cx) ** 2 + (y - cy) ** 2 > radius * radius:
                    i = (y * width + x) * 3
                    rgb[i:i + 3] = b"\x00\x00\x00"


def scale_up(rgb: bytes, width: int, height: int, factor: int) -> tuple[int, int, bytes]:
    if factor == 1:
        return width, height, rgb
    out = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            row += rgb[(y * width + x) * 3:(y * width + x) * 3 + 3] * factor
        out += bytes(row) * factor
    return width * factor, height * factor, bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "build" / "preview"))
    parser.add_argument("--scale", type=int, default=2)
    parser.add_argument("--mem-kb", type=int, default=48, help="LVGL pool size (firmware: sdkconfig.defaults)")
    parser.add_argument("--budget", type=float, default=0.75)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    binary = build(out, args.mem_kb)
    shots = out / "shots"
    shots.mkdir(exist_ok=True)
    for stale in list(shots.glob("*.ppm")) + list(shots.glob("*.png")):
        stale.unlink()
    try:
        result = subprocess.run([str(binary), str(shots)], capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"ERROR: preview program did not finish within {args.timeout} s (hang?)")
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    peak = re.search(r"PEAK used=(\d+) of (\d+)", result.stdout)
    if peak and int(peak.group(1)) > args.budget * int(peak.group(2)):
        raise SystemExit(f"ERROR: LVGL pool peak {peak.group(1)} B exceeds {args.budget:.0%} of {peak.group(2)} B")
    if result.returncode != 0:
        raise SystemExit(f"ERROR: preview program failed ({result.returncode})")
    for ppm in sorted(shots.glob("*.ppm")):
        width, height, rgb = read_ppm(ppm)
        buf = bytearray(rgb)
        mask_corners(buf, width, height, CORNER_RADIUS)
        w2, h2, scaled = scale_up(bytes(buf), width, height, args.scale)
        write_png(ppm.with_suffix(".png"), w2, h2, scaled)
        ppm.unlink()
    print(f"wrote {len(list(shots.glob('*.png')))} PNG file(s) to {shots}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
