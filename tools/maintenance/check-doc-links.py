#!/usr/bin/env python3
"""Check repository-local links in Markdown documentation."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/archive/migrations/claude-layout-2026-07/manifest.json"
INLINE_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
REFERENCE_LINK = re.compile(r"^\s*\[[^\]]+\]:\s*(\S+)", re.MULTILINE)
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "data:")


def markdown_files() -> list[Path]:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z", "*.md"],
        check=True,
        stdout=subprocess.PIPE,
    )
    paths = [ROOT / item.decode("utf-8") for item in result.stdout.split(b"\0") if item]
    for extra in (ROOT / "docs", ROOT / ".claude"):
        if extra.exists():
            paths.extend(extra.rglob("*.md"))
    paths.extend([ROOT / "CLAUDE.md", ROOT / "tools/README.md"])
    return sorted({path.resolve() for path in paths if path.is_file()})


def normalized_target(raw: str) -> str | None:
    target = raw.strip()
    if target.startswith("<") and ">" in target:
        target = target[1 : target.index(">")]
    else:
        target = target.split(maxsplit=1)[0]
    target = unquote(target).replace("\\", "/")
    if not target or target.startswith("#") or target.lower().startswith(EXTERNAL_PREFIXES):
        return None
    target = target.split("#", 1)[0].split("?", 1)[0]
    if not target or any(marker in target for marker in ("*", "${", "$(", "{{", "`")):
        return None
    return target


def migration_maps() -> tuple[dict[str, str], dict[str, str]]:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    old_to_new: dict[str, str] = {}
    new_to_old: dict[str, str] = {}
    for entry in data["entries"]:
        destinations = entry.get("destinations", [])
        if len(destinations) != 1:
            continue
        source = str(entry["source"])
        destination = str(destinations[0])
        old_to_new[source.casefold()] = destination
        new_to_old[destination.casefold()] = source
    return old_to_new, new_to_old


def replace_raw_target(raw: str, replacement: str) -> str:
    fragment = ""
    token = raw.strip()
    if token.startswith("<") and ">" in token:
        end = token.index(">")
        original = token[1:end]
        suffix = token[end + 1 :]
        if "#" in original:
            fragment = "#" + original.split("#", 1)[1]
        return f"<{replacement}{fragment}>{suffix}"
    parts = token.split(maxsplit=1)
    if "#" in parts[0]:
        fragment = "#" + parts[0].split("#", 1)[1]
    suffix = f" {parts[1]}" if len(parts) == 2 else ""
    return f"{replacement}{fragment}{suffix}"


def fix_links() -> int:
    old_to_new, new_to_old = migration_maps()
    old_name_candidates: dict[str, set[str]] = {}
    for old, new in old_to_new.items():
        old_name_candidates.setdefault(Path(old).name.casefold(), set()).add(new)
    old_name_to_new = {
        name: next(iter(candidates))
        for name, candidates in old_name_candidates.items()
        if len(candidates) == 1
    }
    disk_name_candidates: dict[str, set[str]] = {}
    for path in ROOT.rglob("*"):
        if path.is_file() and not any(part in {".git", "build", "build-local-mingw", "vcpkg"} for part in path.parts):
            disk_name_candidates.setdefault(path.name.casefold(), set()).add(path.relative_to(ROOT).as_posix())
    disk_name_to_path = {
        name: next(iter(candidates))
        for name, candidates in disk_name_candidates.items()
        if len(candidates) == 1
    }
    changed_files = 0
    changed_links = 0
    rootish = {"docs", "tools", "src", "res", ".github", "CLAUDE.md", "BUILD.md", "README.md"}

    for source in markdown_files():
        source_relative = source.relative_to(ROOT).as_posix()
        old_source = new_to_old.get(source_relative.casefold())
        if old_source is None:
            continue
        old_parent = Path(old_source).parent
        text = source.read_text(encoding="utf-8", errors="ignore")

        def replace(match: re.Match[str]) -> str:
            nonlocal changed_links
            raw = match.group(1)
            target = normalized_target(raw)
            if target is None:
                return match.group(0)

            target_path = Path(target)
            root_candidate = ROOT / target_path
            if target_path.parts and target_path.parts[0] in rootish and root_candidate.exists():
                new_relative = target_path.as_posix()
            else:
                old_target = Path(os.path.normpath(old_parent / target_path)).as_posix()
                mapped = old_to_new.get(old_target.casefold())
                if mapped is not None:
                    new_relative = mapped
                elif (ROOT / old_target).exists():
                    new_relative = old_target
                else:
                    root_suffix = None
                    for index, part in enumerate(target_path.parts):
                        if part in rootish:
                            candidate = Path(*target_path.parts[index:]).as_posix()
                            if (ROOT / candidate).exists():
                                root_suffix = candidate
                                break
                    filename = target_path.name.casefold()
                    if root_suffix is not None:
                        new_relative = root_suffix
                    elif filename in old_name_to_new:
                        new_relative = old_name_to_new[filename]
                    elif filename in disk_name_to_path:
                        new_relative = disk_name_to_path[filename]
                    else:
                        return match.group(0)

            relative = os.path.relpath(ROOT / new_relative, source.parent).replace("\\", "/")
            updated_raw = replace_raw_target(raw, relative)
            if updated_raw == raw:
                return match.group(0)
            changed_links += 1
            return match.group(0).replace(raw, updated_raw, 1)

        updated = INLINE_LINK.sub(replace, text)
        if updated != text:
            source.write_text(updated, encoding="utf-8", newline="")
            changed_files += 1

    print(f"Rewrote {changed_links} migrated links in {changed_files} files")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fix-migrated", action="store_true", help="rewrite links using the migration manifest")
    args = parser.parse_args()
    if args.fix_migrated:
        return fix_links()

    broken: list[str] = []
    checked = 0
    for source in markdown_files():
        text = source.read_text(encoding="utf-8", errors="ignore")
        targets = [match.group(1) for match in INLINE_LINK.finditer(text)]
        targets.extend(match.group(1) for match in REFERENCE_LINK.finditer(text))
        for raw in targets:
            target = normalized_target(raw)
            if target is None:
                continue
            destination = (ROOT / target.lstrip("/")) if target.startswith("/") else (source.parent / target)
            checked += 1
            if not destination.exists():
                relative_source = source.relative_to(ROOT).as_posix()
                broken.append(f"{relative_source}: {raw} -> {destination.relative_to(ROOT) if destination.is_relative_to(ROOT) else destination}")

    if broken:
        print(f"Markdown link audit FAILED: {len(broken)} broken of {checked} local links", file=sys.stderr)
        for item in broken:
            print(f"- {item}", file=sys.stderr)
        return 1

    print(f"Markdown link audit OK: {checked} local links")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
