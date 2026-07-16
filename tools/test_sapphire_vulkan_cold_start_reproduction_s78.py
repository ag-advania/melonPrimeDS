#!/usr/bin/env python3
"""Freeze and verify Unit-B cold-start crash reproduction (S78-1)."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from sapphire_cold_start_artifacts import (
    build_run_metadata,
    default_artifact_dir,
    write_artifact_bundle,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROM = REPO_ROOT / "tools" / "fixtures" / "synthetic_vulkan_cold_start.nds"
GENERATOR = REPO_ROOT / "tools" / "generate_synthetic_nds_boot_rom.py"
BASELINE_CHECKPOINTS = (
    "[RomBootTrace] Sapphire Vulkan activation complete",
    "[FirstVulkanFrame] producerBegin enter",
    "[RomBootTrace] first RunFrame begin",
    "[FirstGpu2D] before UnitA line=0",
    "[FirstGpu2D] after UnitA line=0",
    "[FirstGpu2D] before UnitB line=0",
)

WINDOWS_ACCESS_VIOLATION = 0xC0000005


def find_vulkan_binary() -> Path | None:
    candidates = [
        REPO_ROOT / "build" / "release-mingw-x86_64" / "melonPrimeDS.exe",
        REPO_ROOT / "build" / "debug-mingw-x86_64" / "melonPrimeDS.exe",
        REPO_ROOT / "build" / "sapphire-parity-linux-Release" / "melonDS",
    ]
    return next((path for path in candidates if path.is_file()), None)


def ensure_fixture_rom() -> Path:
    if not FIXTURE_ROM.is_file():
        subprocess.run(
            [sys.executable, str(GENERATOR), str(FIXTURE_ROM)],
            cwd=REPO_ROOT,
            check=True,
        )
    return FIXTURE_ROM


def is_crash_exit_code(code: int) -> bool:
    unsigned = code & 0xFFFFFFFF
    return unsigned in {
        WINDOWS_ACCESS_VIOLATION,
        0x80000003,
        0xC000001D,
        0xC0000409,
    }


class SapphireVulkanColdStartReproductionS78Tests(unittest.TestCase):
    def test_unit_b_line0_crash_is_reproducible_with_frozen_artifacts(self):
        binary = find_vulkan_binary()
        if binary is None:
            self.skipTest("production Vulkan binary unavailable")

        rom_path = ensure_fixture_rom()
        portable_dir = binary.parent / "portable"
        backup_portable: Path | None = None
        if portable_dir.exists():
            backup_portable = Path(tempfile.mkdtemp(prefix="mpds-portable-backup-"))
            shutil.copytree(portable_dir, backup_portable / "portable")

        portable_dir.mkdir(parents=True, exist_ok=True)
        config_text = "\n".join(
            [
                "3D.Renderer = 5",
                "Screen.UseGL = false",
                "Emu.DirectBoot = true",
                "Emu.ExternalBIOSEnable = false",
                "LimitFPS = false",
                "",
            ]
        )
        config_path = portable_dir / "melonDS.toml"
        config_path.write_text(config_text, encoding="utf-8")

        env = os.environ.copy()
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        env["MELONPRIME_FORCE_VULKAN_RENDERER"] = "1"
        env["MELONPRIME_FORCE_VULKAN_PRESENTER"] = "1"
        env.pop("MELONPRIME_VULKAN_COLD_START_TEST", None)
        env.pop("MELONPRIME_VULKAN_COLD_START_FRAMES", None)

        try:
            proc = subprocess.run(
                [str(binary), str(rom_path)],
                cwd=binary.parent,
                env=env,
                text=True,
                capture_output=True,
                timeout=120,
                check=False,
            )
        finally:
            if config_path.exists():
                config_path.unlink()
            if portable_dir.exists() and not any(portable_dir.iterdir()):
                portable_dir.rmdir()
            if backup_portable is not None:
                if portable_dir.exists():
                    shutil.rmtree(portable_dir)
                shutil.copytree(backup_portable / "portable", portable_dir)
                shutil.rmtree(backup_portable)

        combined = proc.stdout + proc.stderr
        metadata = build_run_metadata(
            repo_root=REPO_ROOT,
            binary=binary,
            rom_path=rom_path,
            config_text=config_text,
            exit_code=proc.returncode,
            combined_output=combined,
            cwd=binary.parent,
        )
        artifact_dir = write_artifact_bundle(
            default_artifact_dir(REPO_ROOT),
            metadata=metadata,
            stdout=proc.stdout,
            stderr=proc.stderr,
        )

        for checkpoint in BASELINE_CHECKPOINTS:
            self.assertIn(checkpoint, combined, msg=f"missing {checkpoint}")

        self.assertTrue(
            is_crash_exit_code(proc.returncode),
            msg=(
                f"expected ACCESS_VIOLATION-like crash, got {proc.returncode}\n"
                f"artifacts={artifact_dir}\n{combined[-8000:]}"
            ),
        )
        self.assertNotIn(
            "[FirstGpu2D] after UnitB line=0",
            combined,
            msg="baseline should still crash before Unit B completes",
        )
        self.assertTrue(
            metadata.minidump_paths or metadata.crash_report_paths,
            msg=f"expected crash artifacts in {artifact_dir}",
        )

        print(f"[ColdStartArtifact] bundle={artifact_dir}")


if __name__ == "__main__":
    unittest.main()
