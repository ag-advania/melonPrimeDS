#!/usr/bin/env python3
"""Cold-start reproduction artifact helpers (S78-1)."""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass(frozen=True)
class ColdStartRunMetadata:
    repo_commit: str
    run_id: str
    binary_path: str
    binary_sha256: str
    rom_path: str
    rom_sha256: str
    config_text: str
    config_sha256: str
    exit_code: int
    minidump_paths: list[str]
    crash_report_paths: list[str]
    timestamp_utc: str


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def git_head_commit(repo_root: Path) -> str:
    try:
        proc = subprocess.run(
            ["git", "-C", str(repo_root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return proc.stdout.strip()


def extract_run_id(output: str) -> str:
    patterns = (
        r"\[RunIdentity\] runId=(\d+)",
        r"\[MelonPrimeCrash\] runId=(\d+)",
        r"buildIdentity\.runId=(\d+)",
    )
    for pattern in patterns:
        match = re.search(pattern, output)
        if match:
            return match.group(1)
    return "unknown"


def artifact_paths_for_run_id(
    *,
    output: str,
    cwd: Path,
    run_id: str,
    glob_suffix: str,
) -> list[str]:
    paths: list[str] = []
    if run_id != "unknown":
        for match in re.finditer(
            rf"(?:minidump|report)=([^\s]*run-{re.escape(run_id)}[^\s]*)",
            output,
        ):
            candidate = cwd / match.group(1)
            if candidate.is_file():
                paths.append(str(candidate.resolve()))

    run_token = f"run-{run_id}"
    for path in cwd.glob(f"melonPrimeDS-*{glob_suffix}"):
        if run_id != "unknown" and run_token not in path.name:
            continue
        resolved = str(path.resolve())
        if resolved not in paths:
            paths.append(resolved)
    return paths


def extract_minidump_paths(output: str, cwd: Path, run_id: str) -> list[str]:
    return artifact_paths_for_run_id(
        output=output,
        cwd=cwd,
        run_id=run_id,
        glob_suffix="*.dmp",
    )


def extract_crash_report_paths(output: str, cwd: Path, run_id: str) -> list[str]:
    return artifact_paths_for_run_id(
        output=output,
        cwd=cwd,
        run_id=run_id,
        glob_suffix="*.crash.txt",
    )


def build_run_metadata(
    *,
    repo_root: Path,
    binary: Path,
    rom_path: Path,
    config_text: str,
    exit_code: int,
    combined_output: str,
    cwd: Path,
) -> ColdStartRunMetadata:
    run_id = extract_run_id(combined_output)
    return ColdStartRunMetadata(
        repo_commit=git_head_commit(repo_root),
        run_id=run_id,
        binary_path=str(binary.resolve()),
        binary_sha256=sha256_file(binary),
        rom_path=str(rom_path.resolve()),
        rom_sha256=sha256_file(rom_path),
        config_text=config_text,
        config_sha256=sha256_text(config_text),
        exit_code=exit_code,
        minidump_paths=extract_minidump_paths(combined_output, cwd, run_id),
        crash_report_paths=extract_crash_report_paths(combined_output, cwd, run_id),
        timestamp_utc=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    )


def write_artifact_bundle(
    artifact_dir: Path,
    *,
    metadata: ColdStartRunMetadata,
    stdout: str,
    stderr: str,
) -> Path:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    (artifact_dir / "metadata.json").write_text(
        json.dumps(asdict(metadata), indent=2) + "\n",
        encoding="utf-8",
    )
    (artifact_dir / "stdout.log").write_text(stdout, encoding="utf-8")
    (artifact_dir / "stderr.log").write_text(stderr, encoding="utf-8")
    (artifact_dir / "combined.log").write_text(stdout + stderr, encoding="utf-8")
    summary = "\n".join(
        [
            f"[ColdStartArtifact] commit={metadata.repo_commit}",
            f"[ColdStartArtifact] runId={metadata.run_id}",
            f"[ColdStartArtifact] binary={metadata.binary_path}",
            f"[ColdStartArtifact] binarySha256={metadata.binary_sha256}",
            f"[ColdStartArtifact] rom={metadata.rom_path}",
            f"[ColdStartArtifact] romSha256={metadata.rom_sha256}",
            f"[ColdStartArtifact] configSha256={metadata.config_sha256}",
            f"[ColdStartArtifact] exitCode={metadata.exit_code}",
            f"[ColdStartArtifact] minidumps={metadata.minidump_paths}",
            f"[ColdStartArtifact] crashReports={metadata.crash_report_paths}",
            "",
        ]
    )
    (artifact_dir / "summary.txt").write_text(summary, encoding="utf-8")
    return artifact_dir


def default_artifact_dir(repo_root: Path) -> Path:
    env = os.environ.get("MELONPRIME_VULKAN_COLD_START_ARTIFACT_DIR")
    if env:
        return Path(env)
    return repo_root / "build" / "cold-start-artifacts" / metadata_stamp()


def metadata_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
