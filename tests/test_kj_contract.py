#!/usr/bin/env python3
"""限定猜拳的静态约束：固件不再进入 baseline 测试界面、线程模型、看板协议两端一致、看板离线可用。"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end():index]
    raise AssertionError(f"function is unterminated: {name}")


class FirmwareUiContract(unittest.TestCase):
    def test_baseline_test_ui_is_not_built(self):
        cmake = "\n".join(line.split("#", 1)[0] for line in read("main/CMakeLists.txt").splitlines())
        self.assertNotRegex(cmake, r"demo_\w+\.c|ui_pixel")
        self.assertIn('file(GLOB KJ_SOURCES "${CMAKE_CURRENT_LIST_DIR}/kj_*.c")', cmake)
        main = read("main/main.c")
        for header in ("demo.h", "demo_navigation.h", "ui_pixel.h"):
            self.assertNotIn(f'#include "{header}"', main)
        for path in sorted((ROOT / "main").glob("kj_*.[ch]")):
            self.assertNotRegex(path.read_text(encoding="utf-8"), r"ui_pixel_|demo_navigation",
                                f"{path.name} reuses the baseline demo shell")

    def test_lvgl_only_touched_under_lock(self):
        main = read("main/main.c")
        for match in re.finditer(r"\bkj_ui_(render|init)\(", main):
            before = main[:match.start()]
            lock = before.rfind("bsp_lvgl_lock(")
            unlock = before.rfind("bsp_lvgl_unlock(")
            line = main.count("\n", 0, match.start()) + 1
            self.assertGreater(lock, unlock, f"main.c:{line} touches LVGL without holding the lock")
        # 按键与无线回调只拷贝入队
        for cb in ("on_key", "on_radio"):
            body = function_body(main, cb)
            self.assertIn("xQueueSend", body)
            self.assertNotRegex(body, r"\blv_|kj_ui_|kj_client_|kj_server_|bsp_audio|kj_sound_play")

    def test_radio_frames_fit_espnow(self):
        proto = read("main/kj_proto.h")
        frame_max = int(re.search(r"#define KJ_FRAME_MAX\s+(\d+)", proto).group(1))
        self.assertLessEqual(frame_max, 250)   # ESP-NOW v1 单帧上限

    def test_wireless_config(self):
        defaults = read("sdkconfig.defaults")
        self.assertIn("CONFIG_BT_ENABLED=n", defaults)
        self.assertRegex(defaults, r"CONFIG_LV_MEM_SIZE_KILOBYTES=\d+")
        self.assertIn("CONFIG_LV_USE_FONT_PLACEHOLDER=y", defaults)


class BoardProtocolContract(unittest.TestCase):
    def setUp(self):
        self.board_c = read("main/kj_board.c")
        self.html = read("tools/kj_board/index.html")

    def test_prefix_matches(self):
        prefix = re.search(r'#define KJ_BOARD_PREFIX\s+"([^"]+)"', read("main/kj_board.h")).group(1)
        self.assertEqual(prefix, "@KJ ")
        self.assertIn('"@KJ {"', self.html)
        self.assertIn("`@KJ ${cmd}\\n`", self.html)

    def test_every_command_is_reachable_from_the_board(self):
        table = re.findall(r'\{\s*"([a-z+\-]+)",\s*KJ_CMD_\w+\s*\}', self.board_c)
        self.assertEqual(set(table), {"start", "end", "new", "reset", "bot+", "bot-", "sync", "kick"})
        for word in table:
            if word == "kick":
                self.assertIn("send(`kick ${", self.html)
            elif word == "sync":
                self.assertIn('send("sync")', self.html)
            else:
                self.assertIn(f'data-cmd="{word}"', self.html)

    def test_board_understands_every_status_and_event(self):
        statuses = re.search(r"names\[KJ_ST_COUNT\]\s*=\s*\{(.*?)\};", self.board_c, re.S).group(1)
        for name in re.findall(r'"(\w+)"', statuses):
            self.assertRegex(self.html, rf"\b{name}:", f"board lacks status {name}")
        events = re.findall(r'case KJ_EV_\w+: return "(\w+)";', self.board_c)
        self.assertGreaterEqual(len(events), 20)
        for name in events:
            self.assertRegex(self.html, rf"\b{name}:\s*(\(\)|e)\s*=>", f"board lacks event text {name}")

    def test_board_is_self_contained(self):
        # 不加载任何外部脚本 / 样式 / 字体：离线也能用，也不会把对局数据发到网上。
        self.assertNotRegex(self.html, r'(src|href)\s*=\s*["\']https?://')
        self.assertNotRegex(self.html, r"fetch\(|XMLHttpRequest|WebSocket")
        # 浏览器存储只放名字与偏好，读写都包在 try/catch 里
        self.assertIn("try { if (val === undefined) return localStorage.getItem(key)", self.html)


if __name__ == "__main__":
    unittest.main()
