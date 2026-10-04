#!/usr/bin/env python3
"""乐器节拍器自动关机路径的静态约束：唤醒源、终端关闭顺序与失败回退。"""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("deep_sleep_contract", ROOT / "tests" / "test_deep_sleep_contract.py")
contract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(contract)


class MetronomePowerOffContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = (ROOT / "main" / "mn_app.c").read_text(encoding="utf-8")
        cls.power_off = contract.function_body(cls.app, "power_off")
        cls.button = (ROOT / "components" / "bsp" / "src" / "bsp_button.c").read_text(encoding="utf-8")

    def test_terminal_shutdown_order(self) -> None:
        # 先保存、释放按键节点并配置低电平唤醒，再让音频任务停手，然后按基线顺序关闭外设。
        calls = [
            "mn_store_flush(",
            "bsp_button_prepare_deep_sleep(",
            "esp_deep_sleep_enable_gpio_wakeup(",
            "mn_audio_halt(",
            "bsp_battery_sleep()",
            "bsp_audio_sleep()",
            "bsp_audio_prepare_deep_sleep()",
            "bsp_i2c_prepare_deep_sleep()",
            "bsp_lvgl_lock(1000)",
            "bsp_display_prepare_deep_sleep()",
            "esp_deep_sleep_start()",
        ]
        positions = [self.power_off.index(call) for call in calls]
        self.assertEqual(positions, sorted(positions))
        # 深睡入口意外返回时必须重启（拿不到 LVGL 锁的失败分支也会更早出现一次 esp_restart）。
        self.assertGreater(self.power_off.rindex("esp_restart()"), positions[-1])

    def test_wake_source_is_a_pin_mask_on_low_level(self) -> None:
        # 第一个参数是引脚位掩码而不是引脚号；传引脚号 0 会让唤醒源配置失败、睡了按不醒。
        self.assertIn("esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW)",
                      self.power_off)

    def test_held_key_or_wake_failure_cancels_instead_of_sleeping(self) -> None:
        self.assertIn("level == 0", self.power_off)
        self.assertGreaterEqual(self.power_off.count("cancel_power_off("), 3)
        cancel = contract.function_body(self.app, "cancel_power_off")
        self.assertIn("bsp_button_init(", cancel)
        self.assertIn("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO)", cancel)

    def test_button_pin_is_released_before_reconfiguring(self) -> None:
        body = contract.function_body(self.button, "bsp_button_prepare_deep_sleep")
        self.assertLess(body.index("button_cleanup()"), body.index("gpio_config("))
        self.assertIn("GPIO_MODE_INPUT", body)
        self.assertIn("1ULL << BSP_BTN_GPIO", body)


if __name__ == "__main__":
    unittest.main(verbosity=1)
