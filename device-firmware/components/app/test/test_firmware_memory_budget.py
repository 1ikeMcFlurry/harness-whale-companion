"""Resource-budget contracts for the ESP32-C3 production configuration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[3]


def read_config(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("CONFIG_") or "=" not in line:
            continue
        key, raw = line.split("=", 1)
        try:
            values[key] = int(raw)
        except ValueError:
            continue
    return values


class FirmwareMemoryBudgetTest(unittest.TestCase):
    def test_generated_config_reserves_ram_for_connected_ble_capture(self) -> None:
        config = read_config(ROOT / "sdkconfig")
        self.assertEqual(48, config["CONFIG_LV_MEM_SIZE_KILOBYTES"])
        self.assertEqual(1, config["CONFIG_BT_NIMBLE_MAX_CONNECTIONS"])
        self.assertEqual(1, config["CONFIG_BT_NIMBLE_MAX_BONDS"])
        self.assertEqual(3, config["CONFIG_BT_NIMBLE_MAX_CCCDS"])
        self.assertEqual(4, config["CONFIG_BT_CTRL_BLE_MAX_ACT"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
