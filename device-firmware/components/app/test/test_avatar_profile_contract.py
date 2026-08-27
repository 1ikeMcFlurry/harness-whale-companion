import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


class AvatarProfileContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = (ROOT / "components/app/src/app.c").read_text(encoding="utf-8")
        cls.view = (ROOT / "components/platform/platform_esp32/src/jpeg_view.c").read_text(encoding="utf-8")
        cls.store = (ROOT / "components/platform/platform_esp32/src/jpeg_store.c").read_text(encoding="utf-8")
        cls.factory = (ROOT / "components/platform/platform_esp32/include/platform/platform_factory.h").read_text(encoding="utf-8")

    def test_ble_image_sink_is_fullscreen_only(self):
        begin = self.app[self.app.index("static int jsink_begin"):self.app.index("static int jsink_write")]
        self.assertIn("JPEG_SLOT_FULL", begin)
        self.assertNotIn("img_mode", begin)
        self.assertNotIn("JPEG_SLOT_AVATAR", self.app + self.view + self.store + self.factory)

    def test_fullscreen_image_freezes_and_restores_home_navigation(self):
        self.assertIn("s_view_nav_restore_pending = true;", self.app)
        self.assertIn("s_view_nav_restore_pending && !jpeg_view_is_active()", self.app)
        self.assertIn("!platform_button_any_pressed(s_button)", self.app)
        self.assertIn("全屏图片已退出,主页按键导航恢复", self.app)
        failure = self.app[self.app.index("static void on_jpeg_result") :]
        failure = failure[:failure.index("static void on_jpeg_avatar")]
        self.assertIn("platform_lvgl_nav_enable(true);", failure)

    def test_named_avatar_store_drives_boot_json_and_fullscreen_restore(self):
        self.assertIn("avatar_store_init()", self.app)
        self.assertIn("avatar_store_has(s_profile.avatar_name)", self.app)
        self.assertIn("jpeg_view_request_avatar(s_profile.avatar_name)", self.app)
        self.assertIn("avatar_store_get", self.view)
        self.assertIn("jpeg_view_request_avatar", self.view)
        json_path = self.app[self.app.index("char old_avatar"):self.app.index("notify_status(CFG_MSG_JSON, status)")]
        self.assertIn("!avatar_store_has(s_profile.avatar_name)", json_path)
        self.assertIn("memcpy(s_profile.avatar_name, old_avatar", json_path)
        self.assertIn("CFG_ST_ERR_AVATAR", json_path)
        self.assertLess(json_path.index("save_profile()"), json_path.index("jpeg_view_request_avatar"))
        teardown = self.view[self.view.index("static void teardown"):self.view.index("static void tick")]
        self.assertIn("avatar_store_has(s_avatar_name)", teardown)

    def test_profile_broadcast_is_initialized_and_dispatched(self):
        self.assertIn("profile_bcast_init", self.app)
        self.assertIn("BLE_MATCH_PROFILE", self.app)
        self.assertIn("profile_bcast_feed", self.app)
        handler = self.app[self.app.index("static void on_profile_bcast"):self.app.index("static void on_ble_match")]
        for field in ("PB_FIELD_TOKEN", "PB_FIELD_TOKEN_MAX", "PB_FIELD_TIME",
                      "PB_FIELD_NICKNAME", "PB_FIELD_AVATAR_NAME"):
            self.assertIn(field, handler)
        self.assertLess(handler.index("save_profile()"), handler.index("profile_ui_flush_pending()"))

    def test_missing_saved_avatar_is_an_informational_persisted_migration(self):
        start = self.app.index("} else if (!avatar_store_has(s_profile.avatar_name))")
        end = self.app.index("if (platform_lvgl_lock(0))", start)
        migration = self.app[start:end]
        self.assertIn('ESP_LOGI(TAG, "已保存头像', migration)
        self.assertIn("avatar_store_first_name(first_name)", migration)
        self.assertIn("memcpy(s_profile.avatar_name, first_name", migration)
        self.assertIn("save_profile()", migration)
        self.assertIn('ESP_LOGW(TAG, "头像回退落盘失败")', migration)

    def test_profile_broadcast_defers_ui_refresh_until_capture_releases_lvgl(self):
        handler = self.app[self.app.index("static void on_profile_bcast"):self.app.index("static void on_ble_match")]
        self.assertIn("s_profile_ui_pending", handler)
        self.assertIn("PROFILE_UI_REFRESH_NAME", handler)
        self.assertIn("PROFILE_UI_REFRESH_TOKEN", handler)
        self.assertIn("profile_ui_flush_pending()", handler)
        self.assertLess(handler.index("atomic_fetch_or(&s_profile_ui_pending"),
                        handler.index("profile_ui_flush_pending()"))
        abort = self.app[self.app.index("static void capture_abort_active"):self.app.index("static bool capture_end_interrupted")]
        self.assertIn("profile_ui_flush_pending()", abort)
        success = self.app[self.app.index("// END ACK 到达时"):self.app.index("static TickType_t capture_result_wait_ticks")]
        self.assertIn("profile_ui_flush_pending()", success)


if __name__ == "__main__":
    unittest.main()
