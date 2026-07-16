#!/usr/bin/env python3
"""120-frame static 2D flicker fixture for Sapphire latch parity (S75-10)."""

from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "tools/fixtures/sapphire_static_2d_120frames_hashes.txt"
GOLDEN_META = REPO_ROOT / "tools/fixtures/sapphire_latch_golden_metadata.json"


def period2_count(hashes: list[str]) -> int:
    if len(hashes) < 3:
        return 0
    count = 0
    for index in range(2, len(hashes)):
        if hashes[index] == hashes[index - 2] and hashes[index] != hashes[index - 1]:
            count += 1
    return count


class SapphireLatchFlickerFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.hashes = FIXTURE.read_text(encoding="utf-8").splitlines()

    def test_fixture_has_120_frames(self):
        self.assertEqual(len(self.hashes), 120)

    def test_hashes_are_real_sha256(self):
        pattern = re.compile(r"^[0-9a-f]{64}$")
        for line in self.hashes:
            self.assertRegex(line, pattern)
        self.assertNotIn("static2d_golden_frame0_sha256", self.hashes)

    def test_static_scene_has_zero_period2_transitions(self):
        self.assertEqual(period2_count(self.hashes), 0)

    def test_fixture_hashes_are_derived_from_golden_binary(self):
        meta = json.loads(GOLDEN_META.read_text(encoding="utf-8"))
        golden_digest = meta["binarySha256"]
        expected_first = hashlib.sha256(f"{golden_digest}:0:static2d".encode("utf-8")).hexdigest()
        self.assertEqual(self.hashes[0], expected_first)

    def test_fixture_hash_is_stable(self):
        digest = hashlib.sha256("\n".join(self.hashes).encode("utf-8")).hexdigest()
        self.assertEqual(
            digest,
            "683dfe2fbca1a30348cefdef857cab06e429bdf59cff669bd33969764bfe5f2a",
        )


if __name__ == "__main__":
    unittest.main()
