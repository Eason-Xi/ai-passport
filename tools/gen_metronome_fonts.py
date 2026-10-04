#!/usr/bin/env python3
"""乐器节拍器字体子集的生成与校验。

字符集来源：main/mn_strings.h 里的全部字符串字面量 + 可打印 ASCII。
这是应用中唯一允许出现非 ASCII 字符串字面量的文件（日志专用文件除外，见 LOG_ONLY）。

  generate  用 lv_font_conv 1.5.3 从 Source Han Sans SC 生成 assets/fonts/mn_*.c，
            并写出 main/mn_font_glyphs.h（启动自检用的码点表）与 manifest。需要 Node：
              python3 tools/gen_metronome_fonts.py generate \\
                  --lv-font-conv <path/to/lv_font_conv> --font <SourceHanSansSC-Regular.otf>
            生成后立即把 .c 的 cmap 解析回码点核对；源字体缺字会在这里报错。
  check     不需要 Node / 字体文件：把已提交的字体 .c 的 cmap 解析回码点，
            与当前字符集、manifest、码点表逐一对照；并拒绝界面代码里绕过
            mn_strings.h 的非 ASCII 字符串字面量。纳入 tools/validate.sh。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STRINGS = ROOT / "main" / "mn_strings.h"
FONT_DIR = ROOT / "assets" / "fonts"
GLYPH_HEADER = ROOT / "main" / "mn_font_glyphs.h"
MANIFEST = FONT_DIR / "mn_fonts.manifest.json"
CONVERTER_VERSION = "1.5.3"

# 名称、字号、bpp、字符集（text = 正文字符集，big = BPM 大号数字）。
SPECS = [
    ("mn_zh14", 14, 4, "text"),
    ("mn_zh18", 18, 4, "text"),
    ("mn_num88", 88, 4, "big"),
]
BIG_CHARS = "0123456789-"   # BPM 数字；测速未出结果时显示 "--"

# 只写日志、不参与显示的文件：允许中文日志字面量。
LOG_ONLY = {"main.c", "mn_app.c", "mn_audio.c", "mn_store.c", "mn_fonts.c"}

SOURCE_FONT = {
    "family": "Source Han Sans SC",
    "file": "SourceHanSansSC-Regular.otf",
    "version": "2.005 (SubsetOTF/SC)",
    "license": "SIL Open Font License 1.1",
    "url": "https://github.com/adobe-fonts/source-han-sans",
}


# ---------------------------------------------------------------------------
# C 源码词法：去掉注释，提取字符串字面量
# ---------------------------------------------------------------------------

_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'", "a": "\a",
            "b": "\b", "f": "\f", "v": "\v", "?": "?"}


def _decode_c_string(body: str) -> str:
    """把 C 字符串字面量内容（UTF-8 源码）解码为 Python 字符串，处理 \\x 等转义。"""
    out = bytearray()
    i = 0
    while i < len(body):
        ch = body[i]
        if ch != "\\":
            out += ch.encode("utf-8")
            i += 1
            continue
        nxt = body[i + 1]
        if nxt == "x":
            m = re.match(r"[0-9A-Fa-f]{1,2}", body[i + 2:])
            out.append(int(m.group(0), 16))
            i += 2 + len(m.group(0))
        elif nxt in "01234567" and re.match(r"[0-7]{1,3}", body[i + 1:]).group(0) != "0":
            m = re.match(r"[0-7]{1,3}", body[i + 1:])
            out.append(int(m.group(0), 8))
            i += 1 + len(m.group(0))
        else:
            out += _ESCAPES.get(nxt, nxt).encode("utf-8")
            i += 2
    return out.decode("utf-8")


def c_string_literals(text: str) -> list[str]:
    """按 C 词法扫描：跳过 // 与 /* */ 注释和字符常量，返回全部字符串字面量（已解码）。"""
    literals = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text[i] == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            literals.append(_decode_c_string(text[i + 1:j]))
            i = j + 1
        else:
            i += 1
    return literals


# ---------------------------------------------------------------------------
# 字符集
# ---------------------------------------------------------------------------

def text_charset() -> list[int]:
    points = set(range(0x20, 0x7F))
    for literal in c_string_literals(STRINGS.read_text(encoding="utf-8")):
        points.update(ord(ch) for ch in literal if ord(ch) >= 0x20)
    return sorted(points)


def big_charset() -> list[int]:
    return sorted(ord(ch) for ch in BIG_CHARS)


def charset_for(kind: str) -> list[int]:
    return text_charset() if kind == "text" else big_charset()


def to_ranges(points: list[int]) -> str:
    parts, start, prev = [], None, None
    for p in points:
        if start is None:
            start = prev = p
        elif p == prev + 1:
            prev = p
        else:
            parts.append(f"0x{start:X}" if start == prev else f"0x{start:X}-0x{prev:X}")
            start = prev = p
    if start is not None:
        parts.append(f"0x{start:X}" if start == prev else f"0x{start:X}-0x{prev:X}")
    return ",".join(parts)


def stray_literals() -> list[str]:
    """界面 / 逻辑代码里绕过 mn_strings.h 的非 ASCII 字面量（含 \\x 转义写法）。"""
    problems = []
    for path in sorted((ROOT / "main").glob("mn_*.[ch]")):
        if path.name in LOG_ONLY or path == STRINGS or path == GLYPH_HEADER:
            continue
        for literal in c_string_literals(path.read_text(encoding="utf-8")):
            if any(ord(ch) > 0x7E for ch in literal):
                problems.append(f"{path.relative_to(ROOT)}: non-ASCII literal {literal!r} "
                                f"must live in main/mn_strings.h")
    return problems


# ---------------------------------------------------------------------------
# 生成物
# ---------------------------------------------------------------------------

def render_glyph_header() -> str:
    def rows(points: list[int]) -> str:
        items = [f"0x{p:04X}" for p in points]
        return "\n".join("    " + ", ".join(items[i:i + 10]) + "," for i in range(0, len(items), 10))

    text, big = text_charset(), big_charset()
    return (
        "// main/mn_font_glyphs.h —— 由 tools/gen_metronome_fonts.py 生成，请勿手改。\n"
        "// 字体启动自检用的码点表：正文字体（14/18）与 BPM 大号数字。\n"
        "#pragma once\n\n#include <stdint.h>\n\n"
        f"#define MN_GLYPHS_TEXT_COUNT {len(text)}\n"
        f"#define MN_GLYPHS_BIG_COUNT {len(big)}\n\n"
        f"static const uint32_t MN_GLYPHS_TEXT[MN_GLYPHS_TEXT_COUNT] = {{\n{rows(text)}\n}};\n\n"
        f"static const uint32_t MN_GLYPHS_BIG[MN_GLYPHS_BIG_COUNT] = {{\n{rows(big)}\n}};\n"
    )


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_font_codepoints(path: Path) -> set[int]:
    """把 lv_font_conv 生成的 .c 里的 cmap 解析回码点集合；不认识的格式直接报错。"""
    text = path.read_text(encoding="utf-8")
    unicode_lists = {
        name: [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]
        for name, body in re.findall(
            r"static const uint16_t (unicode_list_\d+)\[\]\s*=\s*\{(.*?)\};", text, flags=re.S)
    }
    ofs_lists = {
        name: [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]
        for name, body in re.findall(
            r"static const uint8_t (glyph_id_ofs_list_\d+)\[\]\s*=\s*\{(.*?)\};", text, flags=re.S)
    }
    body = re.search(r"static const lv_font_fmt_txt_cmap_t cmaps\[\]\s*=\s*\{(.*?)\n\};", text, flags=re.S)
    if not body:
        raise ValueError(f"{path.name}: cannot find cmaps[]")
    points: set[int] = set()
    for entry in re.findall(r"\{(.*?)\}", body.group(1), flags=re.S):
        fields = dict(re.findall(r"\.(\w+)\s*=\s*([\w\.]+)", entry))
        if not fields:
            continue
        start = int(fields["range_start"], 0)
        length = int(fields["range_length"], 0)
        kind = fields["type"]
        if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            points.update(range(start, start + length))
        elif kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL":
            # 与 LVGL 查表一致：除第一个码点外，偏移为 0 表示该码点缺字形。
            offsets = ofs_lists[fields["glyph_id_ofs_list"]]
            points.update(start + i for i, off in enumerate(offsets[:length]) if i == 0 or off != 0)
        elif kind == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            points.update(start + off for off in unicode_lists[fields["unicode_list"]])
        else:
            raise ValueError(f"{path.name}: unsupported cmap type {kind}")
    return points


def converter_args(name: str, size: int, bpp: int, points: list[int], font: str, out: str) -> list[str]:
    return ["--font", font, "--range", to_ranges(points), "--size", str(size), "--bpp", str(bpp),
            "--format", "lvgl", "--lv-font-name", name, "--lv-include", "lvgl.h",
            "--no-compress", "--no-kerning", "--output", out]


def manifest_expected() -> dict:
    fonts = []
    for name, size, bpp, kind in SPECS:
        points = charset_for(kind)
        fonts.append({
            "name": name, "file": f"{name}.c", "size": size, "bpp": bpp, "charset": kind,
            "glyph_count": len(points), "ranges": to_ranges(points),
            "command": ["lv_font_conv", *converter_args(name, size, bpp, points, SOURCE_FONT["file"],
                                                         f"assets/fonts/{name}.c")],
        })
    return {"generator": "tools/gen_metronome_fonts.py", "converter": f"lv_font_conv {CONVERTER_VERSION}",
            "source_font": dict(SOURCE_FONT), "fonts": fonts}


def generate(converter: str, font: Path) -> None:
    problems = stray_literals()
    if problems:
        raise SystemExit("\n".join(problems))

    FONT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = manifest_expected()
    manifest["source_font"]["sha256"] = sha256_file(font)
    for entry, (name, size, bpp, kind) in zip(manifest["fonts"], SPECS):
        out = FONT_DIR / f"{name}.c"
        cmd = [converter, *converter_args(name, size, bpp, charset_for(kind), str(font), str(out))]
        subprocess.run(cmd, check=True, capture_output=True)
        # 生成文件头里会记录绝对路径：换成文件名，避免泄露本机目录、保证可复现。
        text = out.read_text(encoding="utf-8").replace(str(font), font.name).replace(str(out), out.name)
        out.write_text(text, encoding="utf-8")
        entry["sha256"] = sha256_file(out)
        # 源字体缺字时 lv_font_conv 只会跳过：生成后立即解析 cmap 核对，缺字直接失败。
        missing = sorted(set(charset_for(kind)) - parse_font_codepoints(out))
        if missing:
            raise SystemExit(f"{name}: source font lacks " + ", ".join(f"U+{p:04X} {chr(p)}" for p in missing))
        print(f"{out.relative_to(ROOT)}: {len(charset_for(kind))} glyphs, {out.stat().st_size} bytes")
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    GLYPH_HEADER.write_text(render_glyph_header(), encoding="utf-8")
    print(f"{GLYPH_HEADER.relative_to(ROOT)} / {MANIFEST.relative_to(ROOT)} updated")


def run_check() -> list[str]:
    problems = stray_literals()
    if not MANIFEST.exists():
        return problems + [f"missing {MANIFEST.relative_to(ROOT)}; run generate"]
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    expected = manifest_expected()
    if manifest.get("converter") != expected["converter"]:
        problems.append("manifest converter version differs")
    if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("source_font", {}).get("sha256", "")):
        problems.append("manifest lacks source font sha256")
    recorded = {f["name"]: f for f in manifest.get("fonts", [])}
    for want in expected["fonts"]:
        name = want["name"]
        path = FONT_DIR / want["file"]
        got = recorded.get(name)
        if not path.exists() or not got:
            problems.append(f"{name}: missing font or manifest entry; run generate")
            continue
        for key in ("size", "bpp", "glyph_count", "ranges", "command"):
            if got.get(key) != want[key]:
                problems.append(f"{name}: manifest {key} is stale (strings changed?); run generate")
        if got.get("sha256") != sha256_file(path):
            problems.append(f"{name}: {path.name} does not match manifest sha256")
        text = path.read_text(encoding="utf-8")
        if re.search(r"(/Users/|/home/|[A-Za-z]:\\\\)", text):
            problems.append(f"{name}: generated file contains an absolute path")
        covered = parse_font_codepoints(path)
        needed = set(charset_for(SPECS[[s[0] for s in SPECS].index(name)][3]))
        missing = sorted(needed - covered)
        if missing:
            problems.append(f"{name}: missing glyphs " + " ".join(f"U+{p:04X}({chr(p)})" for p in missing))
        extra = sorted(covered - needed)
        if extra:
            problems.append(f"{name}: unexpected glyphs " + " ".join(f"U+{p:04X}" for p in extra))
    if not GLYPH_HEADER.exists() or GLYPH_HEADER.read_text(encoding="utf-8") != render_glyph_header():
        problems.append(f"{GLYPH_HEADER.relative_to(ROOT)} is stale; run generate")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    gen = sub.add_parser("generate")
    gen.add_argument("--lv-font-conv", required=True, help=f"lv_font_conv {CONVERTER_VERSION} executable")
    gen.add_argument("--font", required=True, type=Path, help="SourceHanSansSC-Regular.otf")
    sub.add_parser("check")
    args = parser.parse_args(argv)
    if args.cmd == "generate":
        generate(args.lv_font_conv, args.font.resolve())
        return 0
    problems = run_check()
    for p in problems:
        print(f"ERROR: {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"Metronome fonts: PASS ({len(text_charset())} text glyphs x 2 sizes, {len(big_charset())} big glyphs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
