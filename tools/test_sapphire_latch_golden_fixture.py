#!/usr/bin/env python3
"""Golden snapshot fixture contract for Sapphire latch parity (S74-11)."""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "tools/fixtures/sapphire_latch_golden_metadata.json"
GENERATED = REPO_ROOT / "src/frontend/qt_sdl/SapphireGenerated/SapphireFrameLatchCore.cpp"


def stable_hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class SapphireLatchGoldenFixtureTests(unittest.TestCase):
    def test_fixture_metadata_matches_pinned_upstream(self):
        data = json.loads(FIXTURE.read_text(encoding="utf-8"))
        self.assertEqual(data["upstreamTag"], "0.7.0.rc4")
        self.assertEqual(
            data["upstreamFrontendCommit"],
            "2c10e59d7209d354e90d9ef4228330bac3f6e794",
        )
        self.assertGreaterEqual(len(data["compareFields"]), 10)

    def test_generated_core_contains_snapshot_field_writes(self):
        core = GENERATED.read_text(encoding="utf-8")
        for field in json.loads(FIXTURE.read_text(encoding="utf-8"))["compareFields"]:
            self.assertIn(field, core)

    def test_golden_field_set_is_stable(self):
        data = json.loads(FIXTURE.read_text(encoding="utf-8"))
        digest = stable_hash("\n".join(data["compareFields"]))
        self.assertEqual(
            digest,
            "ddae2448cceb63837128b213b43fc50363e5d0b5597f8a27b9ae6444ed3739ef",
        )


if __name__ == "__main__":
    unittest.main()
