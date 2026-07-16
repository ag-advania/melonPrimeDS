#!/usr/bin/env python3
"""Golden snapshot fixture contract for Sapphire latch parity (S75-9)."""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE = REPO_ROOT / "tools/fixtures/sapphire_latch_golden_metadata.json"
BINARY = REPO_ROOT / "tools/fixtures/sapphire_static_2d_golden_snapshot.bin"
GENERATED = REPO_ROOT / "src/frontend/qt_sdl/SapphireGenerated/SapphireFrameLatchCore.cpp"


class SapphireLatchGoldenFixtureTests(unittest.TestCase):
    def test_fixture_metadata_matches_pinned_upstream(self):
        data = json.loads(FIXTURE.read_text(encoding="utf-8"))
        self.assertEqual(data["upstreamTag"], "0.7.0.rc4")
        self.assertEqual(
            data["upstreamFrontendCommit"],
            "2c10e59d7209d354e90d9ef4228330bac3f6e794",
        )
        self.assertGreaterEqual(len(data["compareFields"]), 10)

    def test_binary_fixture_matches_metadata(self):
        data = json.loads(FIXTURE.read_text(encoding="utf-8"))
        payload = BINARY.read_bytes()
        self.assertEqual(len(payload), data["binaryByteLength"])
        digest = hashlib.sha256(payload).hexdigest()
        self.assertEqual(digest, data["binarySha256"])
        self.assertEqual(
            digest,
            "b2c5b3e9e04a5e0fbd370cdb911faf24a1baee67a901a7a80029dc6344a6d496",
        )

    def test_generated_core_contains_snapshot_field_writes(self):
        core = GENERATED.read_text(encoding="utf-8")
        for field in json.loads(FIXTURE.read_text(encoding="utf-8"))["compareFields"]:
            self.assertIn(field, core)


if __name__ == "__main__":
    unittest.main()
