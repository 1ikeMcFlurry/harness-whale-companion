"""Source contracts for idle deep sleep and ADC function-key wakeup."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
APP = (ROOT / "components/app/src/app.c").read_text(encoding="utf-8")
BOARD = (ROOT / "components/platform/platform_esp32/include/platform/board_config.h").read_text(encoding="utf-8")
BUTTON = (ROOT / "components/platform/platform_esp32/src/btn_iot_button.c").read_text(encoding="utf-8")


class DeepSleepContractTest(unittest.TestCase):
    def test_idle_enters_gpio_deep_sleep(self) -> None:
        self.assertIn("#define DEEP_SLEEP_TEST_SECONDS  0", BOARD)
        self.assertIn("DEEP_SLEEP_TEST_SECONDS * 1000u", APP)
        self.assertIn("esp_deep_sleep_enable_gpio_wakeup(", APP)
        self.assertIn("ESP_GPIO_WAKEUP_GPIO_LOW", APP)
        self.assertIn("esp_deep_sleep_start()", APP)
        self.assertNotIn("s_screen_off", APP)

    def test_all_adc_function_keys_share_wakeup_node(self) -> None:
        self.assertIn(".btn_wakeup_gpio = 0", BOARD)
        self.assertIn("platform_button_any_pressed", BUTTON)

    def test_adc_polling_stops_and_gpio_is_stable_before_sleep(self) -> None:
        self.assertIn("iot_button_stop()", BUTTON)
        self.assertIn(".mode = GPIO_MODE_INPUT", BUTTON)
        self.assertIn(".pull_up_en = GPIO_PULLUP_ENABLE", BUTTON)
        self.assertGreaterEqual(BUTTON.count("gpio_get_level(c->wake_gpio)"), 2)
        prepare = APP.index("platform_button_prepare_deep_sleep(s_button)")
        sleep = APP.index("esp_deep_sleep_start()", prepare)
        self.assertLess(prepare, sleep)

    def test_navigation_attaches_before_ui_and_enables_after_wakeup_release(self) -> None:
        attach = APP.index("platform_lvgl_attach_buttons(btn, lvdisp);")
        ui = APP.index("ui_profile_create();")
        self.assertLess(attach, ui)
        self.assertIn("if (woke_from_deep_sleep) platform_lvgl_nav_enable(false);", APP)
        release = APP.index("while (platform_button_any_pressed(btn))")
        enable = APP.index("platform_lvgl_nav_enable(true);", release)
        self.assertLess(release, enable)


if __name__ == "__main__":
    unittest.main(verbosity=2)
