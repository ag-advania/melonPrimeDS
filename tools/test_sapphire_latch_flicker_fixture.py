#!/usr/bin/env python3
"""120-frame static 2D flicker fixture for Sapphire latch parity (S74-12)."""

from __future__ import annotations

import hashlib
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "tools/fixtures/sapphire_static_2d_120frames_hashes.txt"


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

    def test_static_scene_has_zero_period2_transitions(self):
        self.assertEqual(period2_count(self.hashes), 0)

    def test_fixture_hash_is_stable(self):
        digest = hashlib.sha256("\n".join(self.hashes).encode("utf-8")).hexdigest()
        self.assertEqual(
            digest,
            "c7c3e9496556e3c9ecd4ed51135494e9ff773de02b640839021b9cf213483b67",
        )


if __name__ == "__main__":
    unittest.main()
