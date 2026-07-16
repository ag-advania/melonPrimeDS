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
        from generate_sapphire_latch_fixtures import build_frame_output_hash

        base = (REPO_ROOT / "tools/fixtures/sapphire_static_2d_golden_snapshot.bin").read_bytes()
        self.assertEqual(self.hashes[0], build_frame_output_hash(0, base))
        self.assertEqual(self.hashes[1], build_frame_output_hash(1, base))

    def test_fixture_hash_is_stable(self):
        digest = hashlib.sha256("\n".join(self.hashes).encode("utf-8")).hexdigest()
        self.assertEqual(
            digest,
            "3426152e1d42e4b95cb2236ffbdc01ab17b28727455dbb6a90d58f618258ac35",
        )


if __name__ == "__main__":
    unittest.main()
