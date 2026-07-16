#!/usr/bin/env python3
"""Real executable Vulkan ROM cold-start integration test (S77-13)."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
FIXTURE_ROM = REPO_ROOT / "tools" / "fixtures" / "synthetic_vulkan_cold_start.nds"
GENERATOR = REPO_ROOT / "tools" / "generate_synthetic_nds_boot_rom.py"

WINDOWS_ACCESS_VIOLATION = 0xC0000005
CHECKPOINTS = {
    "renderer_activation": "[RomBootTrace] Sapphire Vulkan activation complete",
    "producer_begin": "[FirstVulkanFrame] producerBegin enter",
    "first_runframe_begin": "[RomBootTrace] first RunFrame begin",
    "first_runframe_complete": "[RomBootTrace] first RunFrame complete",
    "producer_complete": "[RomBootTrace] first Vulkan producer transaction complete",
    "queue_push": "[VulkanProducer] queuePush",
    "presented_game_frame": re.compile(r"\[VulkanPresent\] frameId=\d+ surfacePresent=1"),
    "cold_start_complete": "[VulkanColdStartTest] complete exitCode=0",
}


def find_vulkan_binary() -> Path | None:
    candidates = [
        REPO_ROOT / "build" / "release-mingw-x86_64" / "melonPrimeDS.exe",
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


class SapphireVulkanColdStartS77Tests(unittest.TestCase):
    def test_real_vulkan_rom_cold_start_reaches_checkpoints(self):
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
        config_path = portable_dir / "melonDS.toml"
        config_path.write_text(
            "\n".join(
                [
                    '3D.Renderer = 5',
                    'Screen.UseGL = false',
                    'Emu.DirectBoot = true',
                    'Emu.ExternalBIOSEnable = false',
                    'LimitFPS = false',
                    "",
                ]
            ),
            encoding="utf-8",
        )

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
        if proc.returncode != 0:
            if is_crash_exit_code(proc.returncode):
                self.fail(
                    f"process crashed with exit code {proc.returncode}\n{combined}"
                )
            self.fail(
                f"process exited with code {proc.returncode}\n{combined}"
            )

        missing: list[str] = []
        for name, pattern in CHECKPOINTS.items():
            if isinstance(pattern, re.Pattern):
                if not pattern.search(combined):
                    missing.append(name)
            elif pattern not in combined:
                missing.append(name)

        self.assertFalse(
            missing,
            msg=f"missing checkpoints: {missing}\n\nlog tail:\n{combined[-8000:]}",
        )

        splash_match = re.search(
            r"\[VulkanColdStartTest\] complete exitCode=0 splashHidden=(\d+)",
            combined,
        )
        self.assertIsNotNone(splash_match, msg=combined[-4000:])
        assert splash_match is not None
        self.assertEqual(
            splash_match.group(1),
            "1",
            msg="splash overlay did not hide after first presented frame",
        )


if __name__ == "__main__":
    unittest.main()
