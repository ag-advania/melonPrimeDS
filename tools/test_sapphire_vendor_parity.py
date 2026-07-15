#!/usr/bin/env python3
"""Unit tests for tools/check_sapphire_vendor_parity.py."""

from __future__ import annotations

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
CHECKER_PATH = REPO_ROOT / "tools" / "check_sapphire_vendor_parity.py"


def load_checker_module():
    spec = importlib.util.spec_from_file_location("check_sapphire_vendor_parity", CHECKER_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class SapphireVendorParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checker = load_checker_module()

    def test_unknown_parity_mode_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            local = root / "local.txt"
            local.write_text("hello", encoding="utf-8")
            entry = {
                "local_path": "local.txt",
                "parity_mode": "mystery_mode",
                "local_sha256": self.checker.sha256_file(local),
            }
            old_root = self.checker.REPO_ROOT
            try:
                self.checker.REPO_ROOT = root
                errors = self.checker.check_entry(entry, verify_upstream=False)
            finally:
                self.checker.REPO_ROOT = old_root
            self.assertTrue(any("unknown parity mode" in error for error in errors))

    def test_exact_upstream_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            local = root / "local.txt"
            upstream = root / "upstream.txt"
            local.write_text("local", encoding="utf-8")
            upstream.write_text("upstream", encoding="utf-8")
            entry = {
                "upstream_repository": "SapphireRhodonite/melonDS-android",
                "upstream_path": "upstream.txt",
                "upstream_sha256": self.checker.sha256_file(upstream),
                "local_path": "local.txt",
                "local_sha256": self.checker.sha256_file(local),
                "parity_mode": "exact_upstream",
            }
            old_root = self.checker.REPO_ROOT
            old_env = os.environ.get("SAPPHIRE_ANDROID_ROOT")
            try:
                self.checker.REPO_ROOT = root
                os.environ["SAPPHIRE_ANDROID_ROOT"] = str(root)
                errors = self.checker.check_entry(entry, verify_upstream=True)
            finally:
                self.checker.REPO_ROOT = old_root
                if old_env is None:
                    os.environ.pop("SAPPHIRE_ANDROID_ROOT", None)
                else:
                    os.environ["SAPPHIRE_ANDROID_ROOT"] = old_env
            self.assertTrue(any("exact_upstream mismatch" in error for error in errors))

    def test_normalized_upstream_algorithm_change_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            local = root / "local.cpp"
            upstream = root / "upstream.cpp"
            local.write_text("void foo() { if (x) return; }\n", encoding="utf-8")
            upstream.write_text("void foo() { if (y) return; }\n", encoding="utf-8")
            entry = {
                "upstream_repository": "SapphireRhodonite/melonDS-android",
                "upstream_path": "upstream.cpp",
                "upstream_sha256": self.checker.sha256_file(upstream),
                "local_path": "local.cpp",
                "local_sha256": self.checker.sha256_file(local),
                "parity_mode": "normalized_upstream",
            }
            old_root = self.checker.REPO_ROOT
            old_env = os.environ.get("SAPPHIRE_ANDROID_ROOT")
            try:
                self.checker.REPO_ROOT = root
                os.environ["SAPPHIRE_ANDROID_ROOT"] = str(root)
                errors = self.checker.check_entry(entry, verify_upstream=True)
            finally:
                self.checker.REPO_ROOT = old_root
                if old_env is None:
                    os.environ.pop("SAPPHIRE_ANDROID_ROOT", None)
                else:
                    os.environ["SAPPHIRE_ANDROID_ROOT"] = old_env
            self.assertTrue(any("normalized_upstream mismatch" in error for error in errors))

    def test_normalized_upstream_adapter_block_only_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            local = root / "local.cpp"
            upstream = root / "upstream.cpp"
            upstream.write_text("void foo() {}\n", encoding="utf-8")
            local.write_text(
                "void foo() {}\n"
                "// MELONPRIME_DESKTOP_ADAPTER_BEGIN\n"
                "void desktopOnly() {}\n"
                "// MELONPRIME_DESKTOP_ADAPTER_END",
                encoding="utf-8",
            )
            entry = {
                "upstream_repository": "SapphireRhodonite/melonDS-android",
                "upstream_path": "upstream.cpp",
                "upstream_sha256": self.checker.sha256_file(upstream),
                "local_path": "local.cpp",
                "local_sha256": self.checker.sha256_file(local),
                "parity_mode": "normalized_upstream",
            }
            old_root = self.checker.REPO_ROOT
            old_env = os.environ.get("SAPPHIRE_ANDROID_ROOT")
            try:
                self.checker.REPO_ROOT = root
                os.environ["SAPPHIRE_ANDROID_ROOT"] = str(root)
                errors = self.checker.check_entry(entry, verify_upstream=True)
            finally:
                self.checker.REPO_ROOT = old_root
                if old_env is None:
                    os.environ.pop("SAPPHIRE_ANDROID_ROOT", None)
                else:
                    os.environ["SAPPHIRE_ANDROID_ROOT"] = old_env
            self.assertEqual(errors, [])

    def test_verify_upstream_without_checkout_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            local = root / "local.cpp"
            local.write_text("void foo() {}\n", encoding="utf-8")
            entry = {
                "upstream_repository": "SapphireRhodonite/melonDS-android",
                "upstream_path": "missing.cpp",
                "upstream_sha256": "0" * 64,
                "local_path": "local.cpp",
                "local_sha256": self.checker.sha256_file(local),
                "parity_mode": "normalized_upstream",
            }
            old_root = self.checker.REPO_ROOT
            old_env = os.environ.pop("SAPPHIRE_ANDROID_ROOT", None)
            try:
                self.checker.REPO_ROOT = root
                errors = self.checker.check_entry(entry, verify_upstream=True)
            finally:
                self.checker.REPO_ROOT = old_root
                if old_env is not None:
                    os.environ["SAPPHIRE_ANDROID_ROOT"] = old_env
            self.assertTrue(any("requires" in error for error in errors))


if __name__ == "__main__":
    raise SystemExit(unittest.main())
