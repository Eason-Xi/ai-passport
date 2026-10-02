#!/usr/bin/env python3
"""字帖资源工具测试：字形包编解码、已提交资源的一致性、字体覆盖与负例、字面量约束。"""

from __future__ import annotations

import importlib.util
import json
import random
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


assets = load("gen_yz_assets")
fonts = load("gen_yz_fonts")


class GlyphCodecTest(unittest.TestCase):
    def test_rle_round_trip(self):
        rng = random.Random(20261001)
        n = assets.GLYPH_SIZE * assets.GLYPH_SIZE
        cases = [[0] * n, [15] * n, [rng.choice([0, 0, 0, 3, 15]) for _ in range(n)],
                 [rng.randrange(16) for _ in range(n)]]
        for pixels in cases:
            blob = assets.rle_encode(pixels)
            self.assertEqual(assets.rle_decode(blob, n), pixels)

    def test_decode_rejects_wrong_length(self):
        n = assets.GLYPH_SIZE * assets.GLYPH_SIZE
        blob = assets.rle_encode([0] * n)
        with self.assertRaises(ValueError):
            assets.rle_decode(blob[:-1], n)
        with self.assertRaises(ValueError):
            assets.rle_decode(blob + b"\x00", n)

    def test_pack_round_trip(self):
        blobs = [b"\x01\x02", b"", b"\xff" * 9]
        self.assertEqual(assets.unpack_bin(assets.pack_bin(blobs)), blobs)


class CommittedAssetsTest(unittest.TestCase):
    def test_glyph_pack_and_catalog_are_consistent(self):
        self.assertEqual(assets.cmd_check(None), 0)

    def test_catalog_entries(self):
        cat = json.loads(assets.CATALOG_JSON.read_text(encoding="utf-8"))
        keys = [c["key"] for c in cat["chapters"]]
        self.assertEqual(len(cat["entries"]), 188)
        seen = set()
        for e in cat["entries"]:
            self.assertIn(e["chapter"], keys)
            self.assertIn(e["trad"], e["phrase"])
            self.assertIn(e["struct"], assets.STRUCT_CODES)
            self.assertIn(e["focus"], assets.FOCUS_CODES)
            x0, y0, x1, y1 = e["box"]
            self.assertTrue(x0 < x1 and y0 < y1)
            source = (e["page"], tuple(e["box"]))
            self.assertNotIn(source, seen, "同一处拓本被裁切了两次")
            seen.add(source)
        # 章节内的字不重复（不同章节可以收同一个字的不同写法，例如“千”）。
        for key in keys:
            chars = [e["trad"] for e in cat["entries"] if e["chapter"] == key]
            self.assertEqual(len(chars), len(set(chars)), key)


class FontTest(unittest.TestCase):
    def test_committed_fonts_pass_check(self):
        self.assertEqual(fonts.run_check(), [])

    def test_text_fonts_cover_catalog(self):
        cat = json.loads(assets.CATALOG_JSON.read_text(encoding="utf-8"))
        needed = {ord(ch) for e in cat["entries"] for k in ("simp", "trad", "pinyin", "phrase") for ch in e[k]}
        for name in ("yz_zh14", "yz_zh18", "yz_zh24"):
            covered = fonts.parse_font_codepoints(fonts.FONT_DIR / f"{name}.c")
            self.assertTrue(needed <= covered, name)
        big = fonts.parse_font_codepoints(fonts.FONT_DIR / "yz_zh32.c")
        self.assertTrue({ord(e["simp"]) for e in cat["entries"]} <= big)

    def test_known_missing_glyph_is_absent(self):
        # 负例：U+9F98（龘）不在任何界面文字里，保证覆盖检查不会无条件通过。
        for name, *_ in fonts.SPECS:
            self.assertNotIn(0x9F98, fonts.parse_font_codepoints(fonts.FONT_DIR / f"{name}.c"))

    def test_stray_literal_detection(self):
        text = 'static const char *a = "\\xe4\\xb8\\xad"; // "注释里的中文不算"\nchar b = \'"\';'
        literals = fonts.c_string_literals(text)
        self.assertEqual(literals, ["中"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
