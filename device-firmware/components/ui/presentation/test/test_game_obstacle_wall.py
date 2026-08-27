"""Source contracts for the runner's rectangular wall obstacles."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[4]
GAME_SOURCE = ROOT / "components/ui/presentation/src/ui_game.c"


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.DOTALL)
    if match is None:
        raise AssertionError(f"function {name} is missing")
    brace = source.find("{", match.start())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"function {name} has no closing brace")


class GameObstacleWallContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = GAME_SOURCE.read_text(encoding="utf-8")

    def test_wall_constants_define_committed_size(self) -> None:
        self.assertRegex(self.source, r"#define\s+OBST_W\s+18\b")
        self.assertRegex(self.source, r"#define\s+OBST_H\s+84\b")
        self.assertRegex(self.source, r"#define\s+OBST_RADIUS\s+5\b")
        self.assertRegex(self.source, r"#define\s+OBST_BORDER\s+3\b")
        self.assertRegex(
            self.source,
            r"#define\s+COL_OBST\s+lv_color_hex\(0xFF3D8B\)",
        )

    def test_obstacle_pool_uses_neon_pink_columns(self) -> None:
        scene = function_body(self.source, "build_scene")
        self.assertIn("make_obstacle(G.scr)", scene)
        obstacle_block = scene[scene.find("for (int i = 0; i < N_OBST; i++)"):]
        self.assertNotIn("lv_label_create", obstacle_block.split("// 玩家", 1)[0])

        obstacle = function_body(self.source, "make_obstacle")
        self.assertIn("COL_OBST_CORE", obstacle)
        self.assertIn("lv_obj_set_style_border_color(o, COL_OBST, 0)", obstacle)
        self.assertNotIn("shadow", obstacle)
        self.assertIn("COL_OBST_HILITE", obstacle)
        self.assertIn("lv_obj_align(hilite, LV_ALIGN_CENTER, 0, 0)", obstacle)

    def test_game_refresh_avoids_expensive_screen_layers(self) -> None:
        self.assertRegex(self.source, r"#define\s+TICK_MS\s+40\b")
        opened = function_body(self.source, "ui_game_open")
        self.assertIn("lv_obj_set_style_clip_corner(G.scr, false, 0)", opened)
        self.assertIn("lv_obj_set_style_radius(G.scr, 0, 0)", opened)

    def test_spawn_uses_fixed_wall_bounds_and_no_keyword_text(self) -> None:
        spawn = function_body(self.source, "spawn_maybe")
        self.assertIn("b->w = OBST_W;", spawn)
        self.assertIn("lane_center(b->lane) - OBST_H / 2", spawn)
        self.assertNotIn("lv_label_set_text", spawn)
        self.assertNotIn("CODE_KW", self.source)
        self.assertNotIn("CODE_COL", self.source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
