from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from installer import install_companion


class ProfilePatchTests(unittest.TestCase):
    def test_appends_managed_block_and_is_idempotent(self):
        original = "- insert:\n  - id: existing\n    name: 'file:///existing.ts'\n"
        url = "file:///C:/Users/test/.dsh/plugins/harness-whale-companion/index.ts"
        updated, changed = install_companion.patch_profile_text(original, url)
        self.assertTrue(changed)
        self.assertIn(install_companion.BEGIN_MARKER, updated)
        self.assertIn(url, updated)

        again, changed_again = install_companion.patch_profile_text(updated, url)
        self.assertFalse(changed_again)
        self.assertEqual(updated, again)

    def test_updates_legacy_entry_without_duplicate(self):
        original = (
            "- insert:\n"
            "  - id: harness-whale-companion\n"
            "    name: 'file:///C:/old/index.ts'\n"
        )
        url = "file:///C:/new/index.ts"
        updated, changed = install_companion.patch_profile_text(original, url)
        self.assertTrue(changed)
        self.assertIn(url, updated)
        self.assertEqual(updated.count("harness-whale-companion"), 1)

    def test_profile_write_creates_backup(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary) / "cordis.patch.yml"
            profile.write_text("# existing\n", encoding="utf-8")
            self.assertTrue(install_companion.patch_profile(profile, "file:///plugin.ts"))
            self.assertEqual(len(list(profile.parent.glob("*.whale-backup-*"))), 1)

    def test_profile_discovery_ignores_node_modules(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            user_profile = root / "profiles" / "web" / "cordis.patch.yml"
            package_profile = root / "profiles" / "node_modules" / "package" / "cordis.patch.yml"
            user_profile.parent.mkdir(parents=True)
            package_profile.parent.mkdir(parents=True)
            user_profile.write_text("", encoding="utf-8")
            package_profile.write_text("", encoding="utf-8")
            self.assertEqual(install_companion.profile_paths(root), [user_profile])


if __name__ == "__main__":
    unittest.main()
