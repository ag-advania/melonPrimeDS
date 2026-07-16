#!/usr/bin/env python3
"""Vulkan ROM cold-start regression gate after S79 crash fix."""

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

CHECKPOINTS = (
    "[RomBootTrace] Sapphire Vulkan activation complete",
    "[FirstVulkanFrame] producerBegin enter",
    "[RomBootTrace] first RunFrame begin",
    "[FirstGpu2D] after UnitB sprites line=1",
    "[FirstGpuLine] StartScanline enter",
    "[FirstGpuLine] StartScanline done",
    "[FirstGpuLine] FinishFrame done",
    "[FirstGpuLine] before CheckDMAs HBlank",
    "[FirstGpuLine] after CheckDMAs HBlank",
    "[RomBootTrace] first RunFrame complete",
    "[VulkanProducer] queuePush",
    "[VulkanColdStartTest] complete exitCode=0",
)


def find_vulkan_binary() -> Path | None:
    candidates = [
        REPO_ROOT / "build" / "release-mingw-x86_64" / "melonPrimeDS.exe",
        REPO_ROOT / "build" / "debug-mingw-x86_64" / "melonPrimeDS.exe",
        REPO_ROOT / "build" / "sapphire-parity-linux-Release" / "melonDS",
        REPO_ROOT / "build" / "sapphire-parity-linux-Debug" / "melonDS",
        REPO_ROOT / "build" / "sapphire-parity-linux-sanitizer-asan-ubsan" / "melonDS",
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


class SapphireVulkanColdStartRegressionS79Tests(unittest.TestCase):
    def test_vulkan_rom_cold_start_regression_gate(self):
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
        env["MELONPRIME_VULKAN_COLD_START_TEST"] = "1"
        env["MELONPRIME_VULKAN_COLD_START_FRAMES"] = "4"

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

        build_match = re.search(r"\[BuildIdentity\] commit=(\S+)", combined)
        self.assertIsNotNone(build_match, msg=combined[-4000:])
        assert build_match is not None
        self.assertNotEqual(build_match.group(1), "unknown")

        self.assertEqual(
            proc.returncode,
            0,
            msg=f"expected cold-start success, got {proc.returncode}\nartifacts={artifact_dir}\n{combined[-8000:]}",
        )

        missing = [checkpoint for checkpoint in CHECKPOINTS if checkpoint not in combined]
        self.assertFalse(
            missing,
            msg=f"missing checkpoints: {missing}\nartifacts={artifact_dir}\n{combined[-8000:]}",
        )

        self.assertRegex(
            combined,
            r"\[VulkanPresent\] frameId=\d+ surfacePresent=1",
            msg=combined[-4000:],
        )

        splash_match = re.search(
            r"\[VulkanColdStartTest\] complete exitCode=0 splashHidden=(\d+)",
            combined,
        )
        self.assertIsNotNone(splash_match, msg=combined[-4000:])
        assert splash_match is not None
        self.assertEqual(splash_match.group(1), "1")

        print(f"[ColdStartRegression] bundle={artifact_dir}")


if __name__ == "__main__":
    unittest.main()
