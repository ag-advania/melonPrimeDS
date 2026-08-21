#!/usr/bin/env python3
"""Audit the provenance and deterministic-phase contract of physical A/B runs."""

from __future__ import annotations

import argparse
import ast
from pathlib import Path


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"{label}: missing {needle!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    runner_path = root / "tools/testing/renderer-physical-ab.ps1"
    summarizer_path = root / "tools/testing/summarize-renderer-physical-ab.py"
    main_path = root / "src/frontend/qt_sdl/main.cpp"
    build_info_path = root / "src/frontend/qt_sdl/MelonPrimeBuildInfo.h"
    for path in (runner_path, summarizer_path, main_path, build_info_path):
        if not path.is_file():
            failures.append(f"missing contract file: {path}")

    if not failures:
        runner = runner_path.read_text(encoding="utf-8")
        summarizer = summarizer_path.read_text(encoding="utf-8")
        main_source = main_path.read_text(encoding="utf-8")
        build_info = build_info_path.read_text(encoding="utf-8")

        for needle in (
            "[string]$ExpectedSourceHead",
            "[switch]$AllowUnverifiedBinary",
            "[switch]$RequireCleanProvenance",
            "'--build-info-json'",
            "Start-Process -FilePath $exe -ArgumentList '--build-info-json'",
            "buildInfoStdout",
            "Get-FileHash -LiteralPath $exe -Algorithm SHA256",
            "Get-FileHash -LiteralPath $romPath -Algorithm SHA256",
            "provenance_verified",
            "build_gates",
            "binary_git_dirty",
            "expected_source_sha",
            "binary_source_sha",
            "checkout_source_sha",
            "require_clean_provenance",
            "checkout_branch",
            "final acceptance requires detached HEAD",
            "runManifest",
            "'measurement_start'",
            "'measurement_end'",
            "'process_exit'",
            "MELONPRIME_PERF_CSV",
        ):
            require(runner, needle, "physical runner", failures)

        for needle in (
            "measurement_start_ticks",
            "measurement_end_ticks",
            "frame_end_ticks",
            "summary.json",
            "summary.md",
            "raster_gpu_time_ns",
            "raster_reuse_wait_us",
            "texture_materialize_retry_success_count",
            "pearson_r",
        ):
            require(summarizer, needle, "physical summarizer", failures)

        for needle in (
            "schema_version",
            "git_sha",
            "--build-info-json",
            "MELONPRIMEDS_BUILD_RENDERER_PERF_TELEMETRY",
            "vulkan_backend",
            "dx12_backend",
        ):
            require(main_source, needle, "binary build-info CLI", failures)
        for needle in (
            "MELONPRIMEDS_GIT_SHA",
            "MELONPRIMEDS_GIT_DIRTY",
            "MELONPRIMEDS_BUILD_TYPE",
            "MELONPRIMEDS_BUILD_VULKAN_LATENCY_CAPTURE",
            "MELONPRIMEDS_BUILD_VULKAN",
            "MELONPRIMEDS_BUILD_DX12",
        ):
            require(build_info, needle, "build-info macro contract", failures)

        try:
            ast.parse(summarizer, filename=str(summarizer_path))
        except SyntaxError as error:
            failures.append(f"physical summarizer syntax error: {error}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("PASS: physical renderer A/B provenance and deterministic-phase contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
