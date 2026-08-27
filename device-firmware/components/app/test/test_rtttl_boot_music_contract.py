"""Source contracts for persistent RTTTL boot music."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]
APP_SOURCE = ROOT / "components/app/src/app.c"


class RtttlBootMusicContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = APP_SOURCE.read_text(encoding="utf-8")

    def test_received_score_is_persisted_before_playback(self) -> None:
        save = "hal_kv_set(s_kv, BOOT_RTTTL_KV_KEY"
        play = "xSemaphoreGive(s_music_sig);"
        score_handler = self.source[self.source.index("if (type == CFG_MSG_SCORE)") :]
        score_handler = score_handler[: score_handler.index("#endif")]
        self.assertIn(save, score_handler)
        self.assertLess(score_handler.index(save), score_handler.index(play))

    def test_boot_only_plays_a_valid_saved_score(self) -> None:
        self.assertIn("hal_kv_get(s_kv, BOOT_RTTTL_KV_KEY", self.source)
        self.assertIn("rtttl_init(&probe", self.source)
        self.assertIn("play_saved_boot_rtttl();", self.source)

    def test_deep_sleep_wakeup_skips_boot_music(self) -> None:
        tail = self.source[self.source.rindex("#if PERIPH_AUDIO && PERIPH_BLE") :]
        self.assertIn("if (!woke_from_deep_sleep)", tail)
        self.assertIn("深睡唤醒,跳过 RTTTL 开机音乐", tail)
        self.assertLess(tail.index("if (!woke_from_deep_sleep)"),
                        tail.index("play_saved_boot_rtttl();"))

    def test_legacy_event_wav_playback_is_removed(self) -> None:
        self.assertNotIn("audio_clip_play(", self.source)
        self.assertNotIn("CFG_MSG_AUDIO_CLIP", self.source)
        self.assertNotIn("CLIP_BOOT", self.source)
        self.assertNotIn("CLIP_CONNECT", self.source)
        self.assertNotIn("CLIP_GAMEOVER", self.source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
