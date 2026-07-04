#!/usr/bin/env python3
"""Validate AI-translated MelonPrime localization JSONL artifacts."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


PLACEHOLDER_RE = re.compile(r"%\d+|%%|\{\d+\}")


def placeholders(text: str) -> list[str]:
    return PLACEHOLDER_RE.findall(text)


def load_jsonl(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open(encoding="utf-8") as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
    return rows


def validate_pair(tasks_path: Path, translated_path: Path) -> list[str]:
    errors: list[str] = []
    tasks = load_jsonl(tasks_path)
    translated = load_jsonl(translated_path)
    if len(tasks) != len(translated):
        errors.append(f"row count mismatch: tasks={len(tasks)} translated={len(translated)}")
        return errors

    for idx, (task, row) in enumerate(zip(tasks, translated), start=1):
        for field in ("surface", "key", "language_id"):
            if row.get(field) != task.get(field):
                errors.append(
                    f"row {idx}: {field} mismatch: expected {task.get(field)!r}, got {row.get(field)!r}"
                )
        text = row.get("text")
        if not isinstance(text, str) or not text:
            errors.append(f"row {idx}: text is empty")
            continue
        expected_placeholders = placeholders(task.get("english", ""))
        actual_placeholders = placeholders(text)
        if expected_placeholders != actual_placeholders:
            errors.append(
                f"row {idx}: placeholder mismatch for {task.get('key')!r}: "
                f"expected {expected_placeholders}, got {actual_placeholders}"
            )
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, default=Path("."))
    ap.add_argument("--language", action="append", required=True)
    args = ap.parse_args()

    repo = args.repo.resolve()
    failed = False
    for language in args.language:
        tasks = repo / "artifacts/localization-ai/by-language" / f"{language}.tasks.jsonl"
        translated = repo / "artifacts/localization-ai/translated" / f"{language}.jsonl"
        errors = validate_pair(tasks, translated)
        if errors:
            failed = True
            print(f"[FAIL] {language}: {len(errors)} issue(s)")
            for issue in errors[:20]:
                print(f"  - {issue}")
            if len(errors) > 20:
                print(f"  ... {len(errors) - 20} more")
        else:
            print(f"[PASS] {language}: translated JSONL matches task structure")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
