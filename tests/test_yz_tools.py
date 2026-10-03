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
        n = assets.STORE_SIZE * assets.STORE_SIZE
        cases = [[0] * n, [15] * n, [rng.choice([0, 0, 0, 3, 15]) for _ in range(n)],
                 [rng.randrange(16) for _ in range(n)]]
        for pixels in cases:
            blob = assets.rle_encode(pixels)
            self.assertEqual(assets.rle_decode(blob, n), pixels)

    def test_decode_rejects_wrong_length(self):
        n = assets.STORE_SIZE * assets.STORE_SIZE
        blob = assets.rle_encode([0] * n)
        with self.assertRaises(ValueError):
            assets.rle_decode(blob[:-1], n)
        with self.assertRaises(ValueError):
            assets.rle_decode(blob + b"\x00", n)

    def test_pack_round_trip(self):
        blobs = [b"\x01\x02", b"", b"\xff" * 9]
        self.assertEqual(assets.unpack_bin(assets.pack_bin(blobs)), blobs)


class ChapterItemsTest(unittest.TestCase):
    """分卷规则：碑文卷首尾相接；分类卷每个不同的字只收一处，优先未残损的一处。"""

    @staticmethod
    def entry(trad, cat, damaged=False):
        e = {"trad": trad, "cat": cat}
        if damaged:
            e["damaged"] = True
        return e

    def book(self, entries, chapters):
        return {"entries": entries, "chapters": chapters}

    def test_ranges_and_categories(self):
        es = [self.entry("一", "num"), self.entry("大", "single", damaged=True), self.entry("之", "en"),
              self.entry("大", "single"), self.entry("一", "num"), self.entry("三", "num"), self.entry("大", "single")]
        chapters = [{"key": "a", "range": [0, 3]}, {"key": "b", "range": [3, 7]},
                    {"key": "num", "order": "一二三"}, {"key": "single"}, {"key": "en"}]
        items = assets.chapter_items(self.book(es, chapters))
        self.assertEqual(items[:2], [[0, 1, 2], [3, 4, 5, 6]])
        self.assertEqual(items[2], [0, 5])          # 第一次出现的“一”、按 order 排列
        self.assertEqual(items[3], [3])             # 第一处“大”残损，改取第二处
        self.assertEqual(items[4], [2])

    def test_rejects_gaps(self):
        es = [self.entry("一", "num"), self.entry("二", "num")]
        with self.assertRaises(SystemExit):        # 第二卷没有接上第一卷
            assets.chapter_items(self.book(es, [{"key": "a", "range": [0, 1]}, {"key": "b", "range": [0, 2]},
                                                {"key": "num"}]))
        with self.assertRaises(SystemExit):        # 碑文卷没有覆盖全部字
            assets.chapter_items(self.book(es, [{"key": "a", "range": [0, 1]}, {"key": "num"}]))
        with self.assertRaises(SystemExit):        # 分类卷没有字
            assets.chapter_items(self.book(es, [{"key": "a", "range": [0, 2]}, {"key": "num"}, {"key": "lr"}]))


class CommittedAssetsTest(unittest.TestCase):
    def setUp(self):
        self.cat = json.loads(assets.CATALOG_JSON.read_text(encoding="utf-8"))
        self.book = self.cat["book"]
        self.entries = self.book["entries"]

    def test_glyph_pack_and_catalog_are_consistent(self):
        self.assertEqual(assets.cmd_check(None), 0)

    def test_full_text(self):
        # 全碑 2025 字：正文 2011 字 + 偈颂七章后的“其一”至“其七”小字（一格两字，各拆成一个字）。
        text = "".join(e["trad"] for e in self.entries)
        self.assertEqual(len(text), 2025)
        self.assertTrue(text.startswith("大唐西京千福寺多寶佛塔感應碑文南陽岑勛撰"))
        self.assertTrue(text.endswith("天寶十一載歲次壬辰四月乙丑朔廿二日戊戌建"
                                      "敕撿挍塔使正議大夫行內侍趙思偘判官內府丞車沖撿挍僧義方河南史華刻"))
        self.assertIn("粵妙法蓮華諸佛之祕藏也多寶佛塔證經之踴現也", text)
        notes = [e for e in self.entries if e.get("note")]
        self.assertEqual("".join(e["trad"] for e in notes), "其一其二其三其四其五其六其七")
        for e in notes:
            self.assertIn("小字", e["source"])
        self.assertEqual(len({e["trad"] for e in self.entries}), 841)

    def test_entries(self):
        keys = {c["key"] for c in self.book["chapters"]}
        seen = set()
        source = self.cat["sources"]["duobao_hd"]
        self.assertEqual(sorted(source["pages"]), [f"{k:02d}" for k in range(1, 43)])
        for k, e in enumerate(self.entries):
            self.assertIn(e["cat"], keys)
            self.assertIn(e["chapter"], keys)
            self.assertIn(e["trad"], e["phrase"])
            self.assertLessEqual(len(e["phrase"]), 6, e["phrase"])      # 目录预览一行放得下
            self.assertIn(e["struct"], assets.STRUCT_CODES)
            self.assertIn(e["focus"], assets.FOCUS_CODES)
            self.assertTrue(e["simp"] and e["pinyin"] and e["source"].startswith("剪裱本 第"))
            self.assertTrue(1 <= e["page"] <= 42)
            x0, y0, x1, y1 = e["box"]
            self.assertTrue(0 <= x0 < x1 <= 700 and 0 <= y0 < y1 <= 1290, k)
            for ex0, ey0, ex1, ey1 in e.get("erase", []):
                self.assertTrue(ex0 < ex1 and ey0 < ey1, k)
            where = (e["page"], tuple(e["box"]))
            self.assertNotIn(where, seen, "同一处拓本被裁切了两次")
            seen.add(where)

    def test_chapters(self):
        items = assets.chapter_items(self.book)
        text = [c for c in self.book["chapters"] if "range" in c]
        self.assertEqual([c["name"] for c in text],
                         ["篇首", "出家", "建塔", "赐额", "舍利", "塔相", "法华", "偈颂", "题记"])
        self.assertEqual([i for idx in items[:len(text)] for i in idx], list(range(len(self.entries))))
        for c, idx in zip(text, items):
            self.assertTrue(all(self.entries[i]["chapter"] == c["key"] for i in idx))
        # 分类卷：每个不同的字恰好一处，残损的字只在没有别的写法时才入选。
        cats = items[len(text):]
        chars = [self.entries[i]["trad"] for idx in cats for i in idx]
        self.assertEqual(len(chars), len(set(chars)))
        self.assertEqual(set(chars), {e["trad"] for e in self.entries})
        for idx in cats:
            for i in idx:
                if self.entries[i].get("damaged"):
                    same = [e for e in self.entries if e["trad"] == self.entries[i]["trad"]]
                    self.assertTrue(all(e.get("damaged") for e in same), self.entries[i]["trad"])
        self.assertEqual([self.entries[i]["trad"] for i in cats[0]], list("一二三四五六七八九十廿百千萬"))


class FontTest(unittest.TestCase):
    def test_committed_fonts_pass_check(self):
        self.assertEqual(fonts.run_check(), [])

    def test_text_fonts_cover_catalog(self):
        book = json.loads(assets.CATALOG_JSON.read_text(encoding="utf-8"))["book"]
        entries = book["entries"]
        needed = {ord(ch) for e in entries for k in ("simp", "trad", "pinyin", "phrase", "source") for ch in e[k]}
        needed |= {ord(ch) for k in ("name", "era", "author", "phrase_tag", "intro", "source_text")
                   for ch in book[k] if ch != "\n"}
        needed |= {ord(ch) for c in book["chapters"] for ch in c["name"]}
        for name in ("yz_zh14", "yz_zh18", "yz_zh24"):
            covered = fonts.parse_font_codepoints(fonts.FONT_DIR / f"{name}.c")
            self.assertTrue(needed <= covered, name)
        big = fonts.parse_font_codepoints(fonts.FONT_DIR / "yz_zh32.c")
        self.assertTrue({ord(e["simp"]) for e in entries} <= big)
        self.assertTrue({ord(ch) for ch in book["name"]} <= big)

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
