#!/usr/bin/env python3
"""把 AI 生成的设备图（黑底、屏幕纯绿）处理成可合成的素材。

输入：一张 bl image edit 输出的 PNG（背景纯黑、屏幕区域纯绿平涂）。
输出（public/gen/device/）：
  device.png   RGBA，背景抠成透明、屏幕区域涂黑（防止绿边漏出），裁到设备外框；
  device.json  图片尺寸与屏幕矩形 {x, y, w, h, r}（像素，相对 device.png）。

用法：.venv/bin/python scripts/process_device.py <生成图.png>
依赖：numpy、Pillow、scipy
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

HERE = Path(__file__).resolve().parents[1]
OUT = HERE / "public" / "gen" / "device"


def corner_radius(mask: np.ndarray, x0: int, y0: int, x1: int, y1: int) -> float:
    """沿四个角的 45° 对角线走到第一个绿色像素，步数 t = r·(1 − 1/√2)。"""
    steps = []
    for cx, cy, dx, dy in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        for t in range(200):
            if mask[cy + dy * t, cx + dx * t]:
                steps.append(t)
                break
    return float(np.median(steps)) / (1 - 2 ** -0.5)


def main() -> int:
    src = Path(sys.argv[1])
    rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.int16)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]

    green = (g > 120) & (g > r * 1.5) & (g > b * 1.5)
    lab, n = ndi.label(green)
    sizes = ndi.sum(green, lab, range(1, n + 1))
    screen = lab == (int(np.argmax(sizes)) + 1)
    ys, xs = np.nonzero(screen)
    sx0, sx1, sy0, sy1 = int(xs.min()), int(xs.max()), int(ys.min()), int(ys.max())
    radius = corner_radius(screen, sx0, sy0, sx1, sy1)

    # 背景：与图片边缘连通的近黑像素。
    dark = rgb.max(axis=2) < 24
    lab, _ = ndi.label(dark)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, list(border))
    fg = ndi.binary_fill_holes(~bg)
    alpha = ndi.gaussian_filter(fg.astype(np.float32), 1.0)

    # 屏幕区域（含抗锯齿绿边）涂成近黑，界面截图会盖在上面。
    screen_pad = ndi.binary_dilation(screen, iterations=4)
    out = rgb.astype(np.uint8).copy()
    out[screen_pad] = (6, 6, 6)

    ys, xs = np.nonzero(alpha > 0.02)
    m = 6
    cx0, cy0 = max(0, int(xs.min()) - m), max(0, int(ys.min()) - m)
    cx1, cy1 = min(rgb.shape[1], int(xs.max()) + m + 1), min(rgb.shape[0], int(ys.max()) + m + 1)
    rgba = np.dstack([out, (np.clip(alpha, 0, 1) * 255).astype(np.uint8)])[cy0:cy1, cx0:cx1]

    OUT.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(OUT / "device.png", optimize=True)
    meta = {
        "w": cx1 - cx0, "h": cy1 - cy0,
        "screen": {"x": sx0 - cx0, "y": sy0 - cy0, "w": sx1 - sx0 + 1, "h": sy1 - sy0 + 1, "r": round(radius, 1)},
        "source": src.name,
    }
    (OUT / "device.json").write_text(json.dumps(meta, indent=2))
    print(json.dumps(meta))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
