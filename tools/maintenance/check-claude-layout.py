#!/usr/bin/env python3
"""Fail when the compact .claude layout or its migration manifest drifts."""

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/archive/migrations/claude-layout-2026-07/manifest.json"
RULE_LIMITS = {
    ".claude/rules/00-core.md": 5 * 1024,
    ".claude/rules/10-code-boundaries.md": 6 * 1024,
    ".claude/rules/20-build-validation.md": 5 * 1024,
    ".claude/rules/30-performance-threading.md": 6 * 1024,
    ".claude/rules/40-platform-safety.md": 6 * 1024,
    ".claude/rules/50-localization-ui.md": 4 * 1024,
}
ALLOWED = set(RULE_LIMITS)
LOCAL_SETTINGS = ".claude/settings.local.json"
EXPECTED_ARCHIVE_SHA256 = "d16d55d052337fd5116b262baf67521a6d7c80e1053e7ec4bb1f1e3adc45ac81"
EXPECTED_PHASE_COUNTS = {"P0": 1, "P1": 9, "P3": 13, "P4": 10, "P5": 37, "P6": 65, "P7": 19}
EXPECTED_PLACEMENT_COUNTS = {"docs": 102, "tools": 44, "delete/local": 8}
EXECUTABLE_SUFFIXES = {".bat", ".ps1", ".py", ".sh", ".command"}
LEGACY_REFERENCE = re.compile(
    r"\.claude[\\/](?:skills|features|mphKnowledge|proposals|"
    r"rules[\\/](?:notes|plan|completed))(?:[\\/]|\b)"
)


def git_paths(*args: str) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(ROOT), *args, "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return [item.decode("utf-8") for item in result.stdout.split(b"\0") if item]


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def main() -> int:
    errors: list[str] = []
    tracked = set(git_paths("ls-files"))

    disk_claude = set()
    for path in (ROOT / ".claude").rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT).as_posix()
        if relative != LOCAL_SETTINGS:
            disk_claude.add(relative)
    if disk_claude != ALLOWED:
        fail(errors, f".claude file set differs: missing={sorted(ALLOWED - disk_claude)}, unexpected={sorted(disk_claude - ALLOWED)}")

    markdown_bytes = sum((ROOT / path).stat().st_size for path in disk_claude if path.endswith(".md"))
    if markdown_bytes > 32 * 1024:
        fail(errors, f".claude Markdown is {markdown_bytes} bytes; limit is 32768")

    executables = sorted(path for path in disk_claude if Path(path).suffix.lower() in EXECUTABLE_SUFFIXES)
    if executables:
        fail(errors, f"executables remain in .claude: {executables}")

    for relative, limit in RULE_LIMITS.items():
        path = ROOT / relative
        if path.is_file() and path.stat().st_size > limit:
            fail(errors, f"{relative} is {path.stat().st_size} bytes; limit is {limit}")

    claude_index = ROOT / "CLAUDE.md"
    if not claude_index.is_file():
        fail(errors, "CLAUDE.md is missing")
    else:
        index_targets = set(re.findall(r"\]\(([^)]+)\)", claude_index.read_text(encoding="utf-8")))
        if index_targets != ALLOWED:
            fail(errors, f"CLAUDE.md must index exactly the six rules: {sorted(index_targets)}")

    data: dict[str, object] = {}
    if not MANIFEST.is_file():
        fail(errors, f"missing migration manifest: {MANIFEST.relative_to(ROOT)}")
        entries: list[dict[str, object]] = []
    else:
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
        entries = data.get("entries", [])
        if len(entries) != 154:
            fail(errors, f"manifest has {len(entries)} entries; expected 154")

    if data:
        if data.get("source_archive_sha256") != EXPECTED_ARCHIVE_SHA256:
            fail(errors, "manifest source archive SHA-256 differs from the audited ZIP")
        if data.get("source_file_count") != 154:
            fail(errors, f"manifest source_file_count is {data.get('source_file_count')}; expected 154")
        byte_total = sum(int(entry["bytes"]) for entry in entries)
        line_total = sum(int(entry["lines"]) for entry in entries)
        if byte_total != 1_907_333 or data.get("source_bytes") != byte_total:
            fail(errors, f"manifest byte total is {byte_total}; expected 1907333")
        if line_total != 39_398:
            fail(errors, f"manifest line total is {line_total}; expected 39398")
        phase_counts = dict(Counter(str(entry["phase"]) for entry in entries))
        if phase_counts != EXPECTED_PHASE_COUNTS:
            fail(errors, f"manifest phase counts differ: {phase_counts}")
        placement_counts = Counter()
        for entry in entries:
            destination = str(entry["destination"])
            if destination.startswith("docs/"):
                placement_counts["docs"] += 1
            elif destination.startswith("tools/"):
                placement_counts["tools"] += 1
            else:
                placement_counts["delete/local"] += 1
        if dict(placement_counts) != EXPECTED_PLACEMENT_COUNTS:
            fail(errors, f"manifest placement counts differ: {dict(placement_counts)}")
        if sum(len(entry.get("destinations", [])) for entry in entries) != 147:
            fail(errors, "manifest concrete destination count differs from 147")
        sources = [str(entry["source"]) for entry in entries]
        if len(set(sources)) != len(sources):
            fail(errors, "manifest contains duplicate source paths")

    for entry in entries:
        source = str(entry["source"])
        if source != LOCAL_SETTINGS and (ROOT / source).exists():
            fail(errors, f"legacy source remains in the working tree: {source}")
        for destination in entry.get("destinations", []):
            destination_path = ROOT / str(destination)
            if not destination_path.exists():
                fail(errors, f"manifest destination missing: {destination}")

    scan_paths = tracked | ALLOWED | {
        "CLAUDE.md",
        MANIFEST.relative_to(ROOT).as_posix(),
        "tools/maintenance/check-claude-layout.py",
    }
    for relative in sorted(scan_paths):
        if relative == MANIFEST.relative_to(ROOT).as_posix():
            continue
        path = ROOT / relative
        if not path.is_file() or path.stat().st_size > 2_000_000:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        match = LEGACY_REFERENCE.search(text)
        if match:
            fail(errors, f"legacy .claude reference in {relative}: {match.group(0)}")

    workflow_reference = re.compile(
        r"tools[\\/](?:ci[\\/]audits|codegen[\\/]hud)|docs/generated/hud"
    )
    for relative in (".github/workflows/build-windows.yml", ".github/workflows/build-ubuntu.yml"):
        path = ROOT / relative
        if not path.is_file():
            fail(errors, f"workflow missing: {relative}")
            continue
        text = path.read_text(encoding="utf-8")
        count = len(workflow_reference.findall(text))
        if count != 9:
            fail(errors, f"{relative} has {count} migrated audit/generator/report references; expected 9")
        if ".claude" in text:
            fail(errors, f"legacy .claude reference remains in {relative}")
        if "tools/archive" in text or "tools\\archive" in text:
            fail(errors, f"archive tool is invoked by {relative}")

    if errors:
        print("Claude layout audit FAILED", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(
        f"Claude layout audit OK: {len(entries)} manifest entries, "
        f"{markdown_bytes} .claude Markdown bytes, 9+9 workflow references"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
