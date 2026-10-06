#!/usr/bin/env python3
"""ASMR 声景播放器字体资产测试：覆盖、负例、过期检测、字面量约束、C 词法。"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_asmr_fonts", ROOT / "tools" / "gen_asmr_fonts.py")
gen = importlib.util.module_from_spec(SPEC)
sys.modules["gen_asmr_fonts"] = gen
SPEC.loader.exec_module(gen)

TEXT_FONTS = ("as_zh14", "as_zh18", "as_zh28")


class CharsetTest(unittest.TestCase):
    def test_committed_assets_pass_check(self):
        self.assertEqual(gen.run_check(), [])

    def test_every_text_font_covers_every_ui_string(self):
        needed = set(gen.text_charset())
        for name in TEXT_FONTS:
            covered = gen.parse_font_codepoints(gen.FONT_DIR / f"{name}.c")
            self.assertTrue(needed <= covered, name)

    def test_known_missing_glyph_is_absent(self):
        # 负例：U+9F98（龘）不在界面文字里，也不应出现在任何字体中，保证覆盖检查不会无条件通过。
        for name in (*TEXT_FONTS, "as_num48"):
            covered = gen.parse_font_codepoints(gen.FONT_DIR / f"{name}.c")
            self.assertNotIn(0x9F98, covered)

    def test_big_font_has_only_digits_colon_and_dash(self):
        covered = gen.parse_font_codepoints(gen.FONT_DIR / "as_num48.c")
        self.assertEqual(covered, {ord(c) for c in "0123456789:-"})

    def test_charset_contains_core_ui_text(self):
        chars = {chr(p) for p in gen.text_charset()}
        for text in ("细雨", "海浪", "篝火", "溪流", "山风", "虫鸣", "颂钵", "白噪音", "粉噪音", "棕噪音",
                     "调音台", "睡眠定时", "呼吸引导", "吸气", "屏息", "呼气", "晚安", "电量过低",
                     "音频不可用", "▶", "▲", "▼", "·"):
            self.assertTrue(set(text) <= chars, text)

    def test_glyph_header_matches(self):
        self.assertEqual(gen.GLYPH_HEADER.read_text(encoding="utf-8"), gen.render_glyph_header())


class LexerTest(unittest.TestCase):
    def test_comments_and_chars_are_skipped(self):
        src = '// "注释"\n/* "块注释" */ char c = \'"\'; const char *s = "a\\"b" "中\\x41";'
        self.assertEqual(gen.c_string_literals(src), ['a"b', "中A"])

    def test_utf8_hex_escape_decodes(self):
        self.assertEqual(gen.c_string_literals('"\\xE2\\x96\\xB2"'), ["▲"])

    def test_no_stray_literals_in_app_sources(self):
        self.assertEqual(gen.stray_literals(), [])

    def test_stray_literal_is_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            fake_root = Path(tmp)
            (fake_root / "main").mkdir()
            (fake_root / "main" / "as_fake.c").write_text('// 注释可以\nconst char *t = "中文";\n', encoding="utf-8")
            old_root = gen.ROOT
            try:
                gen.ROOT = fake_root
                problems = gen.stray_literals()
            finally:
                gen.ROOT = old_root
            self.assertEqual(len(problems), 1)
            self.assertIn("as_fake.c", problems[0])

    def test_ranges(self):
        self.assertEqual(gen.to_ranges([0x20, 0x21, 0x22, 0x30, 0x4E2D]), "0x20-0x22,0x30,0x4E2D")


if __name__ == "__main__":
    unittest.main(verbosity=1)
