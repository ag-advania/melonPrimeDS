#!/usr/bin/env python3
"""Generate MelonPrimeGitBuildIdentity.h at build time (S80-6)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def git_output(repo: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", *args],
        cwd=repo,
        text=True,
        stderr=subprocess.DEVNULL,
    ).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    full = short = branch = "unknown"
    dirty = 0

    if (repo / ".git").exists():
        try:
            full = git_output(repo, "rev-parse", "HEAD")
            short = git_output(repo, "rev-parse", "--short=12", "HEAD")
            branch = git_output(repo, "rev-parse", "--abbrev-ref", "HEAD")
            dirty = 1 if git_output(repo, "status", "--porcelain") else 0
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "\n".join(
            [
                "#pragma once",
                "",
                f'#define MELONPRIME_GIT_COMMIT_FULL "{full}"',
                f'#define MELONPRIME_GIT_COMMIT_SHORT "{short}"',
                f'#define MELONPRIME_GIT_BRANCH "{branch}"',
                f"#define MELONPRIME_GIT_DIRTY {dirty}",
                "",
            ]
        ),
        encoding="utf-8",
    )
    print(f"[generate_build_identity] wrote {args.output} commit={short} branch={branch} dirty={dirty}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
