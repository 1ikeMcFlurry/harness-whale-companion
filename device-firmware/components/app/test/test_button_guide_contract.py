"""Source contracts for the post-self-test boot button guide."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
APP = (ROOT / "components/app/src/app.c").read_text(encoding="utf-8")
GUIDE = (ROOT / "components/ui/presentation/src/ui_button_guide.c").read_text(encoding="utf-8")


class ButtonGuideContractTest(unittest.TestCase):
    def test_default_is_five_boots(self) -> None:
        self.assertIn("#define GUIDE_COUNT_DEFAULT 5", APP)
        self.assertNotIn("GUIDE_TIME_DEFAULT", APP)

    def test_factory_entry_resets_persistent_remaining_count(self) -> None:
        self.assertIn("s_guide.remaining = s_guide.count;", APP)
        self.assertIn("save_button_guide()", APP)

    def test_normal_boot_decrements_before_showing_guide(self) -> None:
        self.assertIn("show_boot_guide = true;", APP)
        self.assertIn("!woke_from_deep_sleep && s_guide.remaining > 0", APP)
        self.assertIn("while (!platform_lvgl_lock(500))", APP)
        self.assertIn("按键指引已请求退出,等待截图/绘制释放 LVGL 后关闭", APP)
        decrement = APP.index("s_guide.remaining--;")
        show = APP.index("show_boot_button_guide();")
        self.assertLess(decrement, show)

    def test_deep_sleep_wakeup_skips_guide_and_enters_home(self) -> None:
        self.assertIn("esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO", APP)
        self.assertIn("factory_mode = !woke_from_deep_sleep", APP)
        self.assertIn("while (platform_button_any_pressed(btn))", APP)

    def test_at_commands_expose_count_and_remaining(self) -> None:
        self.assertIn('at_after(b, "guide_count,")', APP)
        self.assertNotIn('at_after(b, "guide_time,")', APP)
        self.assertIn("guide_remaining=%u", APP)

    def test_guide_matches_physical_button_layout(self) -> None:
        self.assertIn('add_edge_callout(true, EDGE_TOP_Y, 104, 98,', GUIDE)
        self.assertIn('"电源键\\n开机:\\n短按0.5s\\n关机:\\n长按2.0s", PINK)', GUIDE)
        self.assertIn("CARD_RIGHT_X - CARD_LEFT_X - CARD_GAP", GUIDE)
        self.assertIn("lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP)", GUIDE)
        self.assertIn("text_y + lv_obj_get_height(text) + 10", GUIDE)
        self.assertIn("lv_obj_align(text, LV_ALIGN_TOP_MID, 0, text_y)", GUIDE)
        self.assertIn("int content_h = lv_obj_get_height(text) + 12", GUIDE)
        self.assertIn("lv_obj_set_height(card, content_h)", GUIDE)
        self.assertIn('add_edge_callout(false, EDGE_TOP_Y, CARD_RIGHT_W, EDGE_H, "上键", GREEN)', GUIDE)
        self.assertIn('add_edge_callout(false, EDGE_MID_Y, CARD_RIGHT_W, EDGE_H, "下键", GREEN)', GUIDE)
        self.assertIn('add_edge_callout(false, EDGE_BOTTOM_Y, CARD_RIGHT_W, 50,', GUIDE)
        self.assertIn('"确定键\\n短按选中\\n长按返回", GREEN)', GUIDE)
        self.assertIn('"按任意功能键开启体验"', GUIDE)
        self.assertNotIn("s_countdown", GUIDE)
        self.assertNotIn("秒后进入主页", GUIDE)
        self.assertIn("lv_obj_delete_async(old)", GUIDE)
        self.assertNotIn("shadow_width", GUIDE)
        self.assertIn("lv_obj_set_style_clip_corner(s_scr, false, 0)", GUIDE)
        self.assertIn("lv_obj_set_style_radius(s_scr, 0, 0)", GUIDE)
        self.assertNotIn('"认识一下按键"', GUIDE)
        self.assertNotIn('"亮条所指就是按键位置"', GUIDE)
        self.assertNotIn("设备外壳", GUIDE)
        self.assertIn("while (!s_button_guide_exit)", APP)
        self.assertIn("e == HAL_BTN_PRESS && index >= 0 && index < 3", APP)


if __name__ == "__main__":
    unittest.main(verbosity=2)
