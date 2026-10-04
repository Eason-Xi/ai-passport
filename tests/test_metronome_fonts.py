#!/usr/bin/env python3
"""乐器节拍器字体资产测试：覆盖、负例、过期检测、字面量约束、C 词法。"""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_metronome_fonts", ROOT / "tools" / "gen_metronome_fonts.py")
gen = importlib.util.module_from_spec(SPEC)
sys.modules["gen_metronome_fonts"] = gen
SPEC.loader.exec_module(gen)

TEXT_FONTS = ("mn_zh14", "mn_zh18")


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
        for name in (*TEXT_FONTS, "mn_num88"):
            covered = gen.parse_font_codepoints(gen.FONT_DIR / f"{name}.c")
            self.assertNotIn(0x9F98, covered)

    def test_big_font_has_only_digits_and_dash(self):
        covered = gen.parse_font_codepoints(gen.FONT_DIR / "mn_num88.c")
        self.assertEqual(covered, {ord(c) for c in "0123456789-"})

    def test_charset_contains_core_ui_text(self):
        chars = {chr(p) for p in gen.text_charset()}
        for text in ("敲击测速", "首拍重音", "三连音", "十六分", "木块", "牛铃", "最急板", "小广板",
                     "演奏中", "音频不可用", "◀", "▶", "▲", "▼", "·"):
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
            (fake_root / "main" / "mn_fake.c").write_text('// 注释可以\nconst char *t = "中文";\n', encoding="utf-8")
            old_root = gen.ROOT
            try:
                gen.ROOT = fake_root
                problems = gen.stray_literals()
            finally:
                gen.ROOT = old_root
            self.assertEqual(len(problems), 1)
            self.assertIn("mn_fake.c", problems[0])

    def test_ranges(self):
        self.assertEqual(gen.to_ranges([0x20, 0x21, 0x22, 0x30, 0x4E2D]), "0x20-0x22,0x30,0x4E2D")


if __name__ == "__main__":
    unittest.main(verbosity=1)
