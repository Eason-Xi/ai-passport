#!/usr/bin/env python3
"""限定猜拳字体子集的生成与校验。

字符集来源：main/kj_strings.h（应用里唯一允许出现非 ASCII 显示字面量的文件）。
  text  全部字符串字面量 + 可打印 ASCII        → kj_zh14 / kj_zh18 / kj_zh26
  big   KJ_BIG_* 宏里的字                      → kj_big48（大标题）
  num   0-9、A-F 与 "-"                        → kj_num56（选手编号、赌局号）
  hand  KJ_HAND_* 宏里的手势（U+270A/270B/270C）→ kj_hand36 / kj_hand64

  generate  用 lv_font_conv 1.5.3 生成 assets/fonts/kj_*.c、main/kj_font_glyphs.h 与 manifest。
            需要 Node 与源字体（均不入库）：
              python3 tools/gen_kj_fonts.py generate --lv-font-conv <lv_font_conv> \\
                  --font-dir <含 SourceHanSansSC-{Regular,Bold,Heavy}.otf 与 NotoEmoji[wght].ttf 的目录>
            Noto Emoji 是可变字体：脚本用 fontTools 先切出 wght=700 的静态实例再转换。
            生成后立刻把 .c 的 cmap 解析回码点核对，源字体缺字直接报错。
  check     不需要 Node / 源字体：核对已提交字体的 cmap、manifest 与码点表是否和当前文字一致，
            并拒绝在 kj_strings.h 之外出现的非 ASCII 字符串字面量。已纳入 tools/validate.sh。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STRINGS = ROOT / "main" / "kj_strings.h"
FONT_DIR = ROOT / "assets" / "fonts"
GLYPH_HEADER = ROOT / "main" / "kj_font_glyphs.h"
MANIFEST = FONT_DIR / "kj_fonts.manifest.json"
CONVERTER_VERSION = "1.5.3"
NUM_CHARS = "0123456789ABCDEF-"

SOURCES = {
    "regular": {"family": "Source Han Sans SC", "file": "SourceHanSansSC-Regular.otf", "weight": "Regular",
                "version": "2.005", "license": "SIL Open Font License 1.1",
                "url": "https://github.com/adobe-fonts/source-han-sans"},
    "bold": {"family": "Source Han Sans SC", "file": "SourceHanSansSC-Bold.otf", "weight": "Bold",
             "version": "2.005", "license": "SIL Open Font License 1.1",
             "url": "https://github.com/adobe-fonts/source-han-sans"},
    "heavy": {"family": "Source Han Sans SC", "file": "SourceHanSansSC-Heavy.otf", "weight": "Heavy",
              "version": "2.005", "license": "SIL Open Font License 1.1",
              "url": "https://github.com/adobe-fonts/source-han-sans"},
    "emoji": {"family": "Noto Emoji", "file": "NotoEmoji[wght].ttf", "weight": "wght=700 static instance",
              "version": "google/fonts main", "license": "SIL Open Font License 1.1",
              "url": "https://github.com/google/fonts/tree/main/ofl/notoemoji"},
}

# 名称、字号、bpp、源字体、字符集
SPECS = [
    ("kj_zh14", 14, 4, "regular", "text"),
    ("kj_zh18", 18, 4, "bold", "text"),
    ("kj_zh26", 26, 4, "heavy", "text"),
    ("kj_big48", 48, 4, "heavy", "big"),
    ("kj_num56", 56, 4, "heavy", "num"),
    ("kj_hand36", 36, 4, "emoji", "hand"),
    ("kj_hand64", 64, 4, "emoji", "hand"),
]

# ---------------------------------------------------------------------------
# C 源码词法：去掉注释，提取字符串字面量（含宏名）
# ---------------------------------------------------------------------------

_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'", "a": "\a",
            "b": "\b", "f": "\f", "v": "\v", "?": "?"}


def _decode_c_string(body: str) -> str:
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


def strip_comments(text: str) -> str:
    """去掉 // 与 /* */ 注释，保留字符串与字符常量。"""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif text[i] in "\"'":
            q = text[i]
            j = i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def c_string_literals(text: str) -> list[str]:
    return [_decode_c_string(m) for m in re.findall(r'"((?:[^"\\\n]|\\.)*)"', strip_comments(text))]


def macro_literals(prefix: str) -> list[str]:
    text = strip_comments(STRINGS.read_text(encoding="utf-8"))
    values = []
    for name, body in re.findall(r'#define\s+(\w+)\s+((?:"(?:[^"\\\n]|\\.)*"\s*)+)', text):
        if name.startswith(prefix):
            values.append("".join(_decode_c_string(s) for s in re.findall(r'"((?:[^"\\\n]|\\.)*)"', body)))
    return values


# ---------------------------------------------------------------------------
# 字符集
# ---------------------------------------------------------------------------

def hand_charset() -> list[int]:
    return sorted({ord(ch) for value in macro_literals("KJ_HAND_") for ch in value})


def text_charset() -> list[int]:
    points = set(range(0x20, 0x7F))
    hands = set(hand_charset())
    for literal in c_string_literals(STRINGS.read_text(encoding="utf-8")):
        points.update(ord(ch) for ch in literal if ord(ch) >= 0x20 and ord(ch) not in hands)
    return sorted(points)


def big_charset() -> list[int]:
    return sorted({ord(ch) for value in macro_literals("KJ_BIG_") for ch in value if ch != " "})


def num_charset() -> list[int]:
    return sorted(ord(ch) for ch in NUM_CHARS)


def charset_for(kind: str) -> list[int]:
    return {"text": text_charset, "big": big_charset, "num": num_charset, "hand": hand_charset}[kind]()


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
    """界面 / 逻辑代码里绕过 kj_strings.h 的非 ASCII 字面量（含 \\x 转义写法）。"""
    problems = []
    for path in sorted((ROOT / "main").glob("kj_*.[ch]")):
        if path in (STRINGS, GLYPH_HEADER):
            continue
        for literal in c_string_literals(path.read_text(encoding="utf-8")):
            if any(ord(ch) > 0x7E for ch in literal):
                problems.append(f"{path.relative_to(ROOT)}: non-ASCII literal {literal!r} "
                                f"must live in main/kj_strings.h")
    return problems


# ---------------------------------------------------------------------------
# 生成物
# ---------------------------------------------------------------------------

def render_glyph_header() -> str:
    def rows(points: list[int]) -> str:
        items = [f"0x{p:04X}" for p in points]
        return "\n".join("    " + ", ".join(items[i:i + 10]) + "," for i in range(0, len(items), 10))

    sets = [("TEXT", text_charset()), ("BIG", big_charset()), ("NUM", num_charset()), ("HAND", hand_charset())]
    body = "".join(f"#define KJ_GLYPHS_{name}_COUNT {len(pts)}\n" for name, pts in sets)
    arrays = "".join(f"\nstatic const uint32_t KJ_GLYPHS_{name}[KJ_GLYPHS_{name}_COUNT] = {{\n{rows(pts)}\n}};\n"
                     for name, pts in sets)
    return ("// main/kj_font_glyphs.h —— 由 tools/gen_kj_fonts.py 生成，请勿手改。\n"
            "// 字体启动自检用的码点表：正文（14/18/26）、大标题、数字、手势。\n"
            "#pragma once\n\n#include <stdint.h>\n\n" + body + arrays)


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


def source_label(key: str) -> str:
    return SOURCES[key]["file"] if key != "emoji" else "NotoEmoji-Bold.ttf"


def manifest_expected() -> dict:
    fonts = []
    for name, size, bpp, src, kind in SPECS:
        points = charset_for(kind)
        fonts.append({
            "name": name, "file": f"{name}.c", "size": size, "bpp": bpp, "source": src, "charset": kind,
            "glyph_count": len(points), "ranges": to_ranges(points),
            "command": ["lv_font_conv", *converter_args(name, size, bpp, points, source_label(src),
                                                         f"assets/fonts/{name}.c")],
        })
    return {"generator": "tools/gen_kj_fonts.py", "converter": f"lv_font_conv {CONVERTER_VERSION}",
            "sources": {k: dict(v) for k, v in SOURCES.items()}, "fonts": fonts}


def instantiate_emoji(variable: Path, out: Path) -> None:
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
    except ImportError as exc:  # pragma: no cover - 开发机依赖
        raise SystemExit("generate needs fontTools (pip install fonttools) to instance Noto Emoji") from exc
    font = TTFont(str(variable))
    static = instancer.instantiateVariableFont(font, {"wght": 700})
    static.save(str(out))


def generate(converter: str, font_dir: Path) -> None:
    problems = stray_literals()
    if problems:
        raise SystemExit("\n".join(problems))
    FONT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = manifest_expected()
    with tempfile.TemporaryDirectory() as tmp:
        paths = {}
        for key, meta in SOURCES.items():
            src = font_dir / meta["file"]
            if not src.exists():
                raise SystemExit(f"missing source font {src}")
            manifest["sources"][key]["sha256"] = sha256_file(src)
            if key == "emoji":
                inst = Path(tmp) / "NotoEmoji-Bold.ttf"
                instantiate_emoji(src, inst)
                paths[key] = inst
            else:
                paths[key] = src
        for entry, (name, size, bpp, src, kind) in zip(manifest["fonts"], SPECS):
            out = FONT_DIR / f"{name}.c"
            cmd = [converter, *converter_args(name, size, bpp, charset_for(kind), str(paths[src]), str(out))]
            subprocess.run(cmd, check=True, capture_output=True)
            # 生成文件头里会记录绝对路径：换成文件名，避免泄露本机目录、保证可复现。
            text = out.read_text(encoding="utf-8")
            text = text.replace(str(paths[src]), source_label(src)).replace(str(out), out.name)
            out.write_text(text, encoding="utf-8")
            entry["sha256"] = sha256_file(out)
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
    for key in SOURCES:
        if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("sources", {}).get(key, {}).get("sha256", "")):
            problems.append(f"manifest lacks sha256 for source font '{key}'")
    recorded = {f["name"]: f for f in manifest.get("fonts", [])}
    for want, spec in zip(expected["fonts"], SPECS):
        name = want["name"]
        path = FONT_DIR / want["file"]
        got = recorded.get(name)
        if not path.exists() or not got:
            problems.append(f"{name}: missing font or manifest entry; run generate")
            continue
        for key in ("size", "bpp", "source", "glyph_count", "ranges", "command"):
            if got.get(key) != want[key]:
                problems.append(f"{name}: manifest {key} is stale (strings changed?); run generate")
        if got.get("sha256") != sha256_file(path):
            problems.append(f"{name}: {path.name} does not match manifest sha256")
        text = path.read_text(encoding="utf-8")
        if re.search(r"(/Users/|/home/|/root/|/tmp/|[A-Za-z]:\\\\)", text):
            problems.append(f"{name}: generated file contains an absolute path")
        covered = parse_font_codepoints(path)
        needed = set(charset_for(spec[4]))
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
    gen.add_argument("--font-dir", required=True, type=Path, help="directory with the source fonts")
    sub.add_parser("check")
    args = parser.parse_args(argv)
    if args.cmd == "generate":
        generate(args.lv_font_conv, args.font_dir.resolve())
        return 0
    problems = run_check()
    for p in problems:
        print(f"ERROR: {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"Limited RPS fonts: PASS ({len(text_charset())} text glyphs x 3 sizes, {len(big_charset())} big, "
          f"{len(num_charset())} digits, {len(hand_charset())} hands x 2)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
