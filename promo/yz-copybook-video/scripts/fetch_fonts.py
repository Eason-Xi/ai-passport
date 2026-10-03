#!/usr/bin/env python3
"""只下载视频里用到的字：扫描 src/ 下所有字符串，向 Google Fonts 请求 text= 子集（woff2）。

Noto Serif SC / Noto Sans SC 即思源宋体 / 思源黑体，SIL Open Font License 1.1。
整套中文字体按 unicode-range 拆成约 200 个文件，渲染时逐个加载很慢；子集只有几十 KB。
改动画面文字后重跑本脚本。输出 public/gen/fonts/*.woff2。

用法：python3 scripts/fetch_fonts.py
"""

from __future__ import annotations

import re
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
OUT = HERE / "public" / "gen" / "fonts"
FACES = [("Noto Serif SC", 600, "serif-600"), ("Noto Serif SC", 900, "serif-900"),
         ("Noto Sans SC", 500, "sans-500"), ("Noto Sans SC", 700, "sans-700")]
UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36"


def collect() -> str:
    chars = {chr(c) for c in range(0x20, 0x7F)}
    for path in (HERE / "src").rglob("*.ts*"):
        chars |= {ch for ch in path.read_text(encoding="utf-8") if ord(ch) > 0x7F}
    chars |= set("·「」《》，。、：★")
    return "".join(sorted(chars))


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    text = collect()
    for family, weight, name in FACES:
        q = urllib.parse.urlencode({"family": f"{family}:wght@{weight}", "text": text, "display": "block"})
        req = urllib.request.Request(f"https://fonts.googleapis.com/css2?{q}", headers={"User-Agent": UA})
        css = urllib.request.urlopen(req, timeout=60).read().decode()
        urls = re.findall(r"url\((https://[^)]+)\)", css)
        assert len(urls) == 1, (name, css[:300])
        data = urllib.request.urlopen(urllib.request.Request(urls[0], headers={"User-Agent": UA}), timeout=60).read()
        (OUT / f"{name}.woff2").write_bytes(data)
        print(f"{name}.woff2 {len(data) // 1024} KB")
    print(f"{len(text)} chars")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
