#!/usr/bin/env bash
# Build only: skip CMake configure (existing build-linux tree must exist).
# Linux VM counterpart of .claude/skills/build-mingw-existing.bat
set -euo pipefail

usage() {
  cat <<EOF
Usage: guest-build-existing.sh [REPO_ROOT] [--verbose] [--jobs N] [--tail N]

Build only: skips CMake configure and apt install. Requires build-linux/build.ninja.
Run guest-build-only.sh once first to configure the tree.

Defaults: --jobs 1, --tail 40 (use --verbose for full compiler output).
EOF
}

REPO_ROOT="${REPO_ROOT:-/mnt/mp}"
JOBS=1
TAIL_LINES=40
VERBOSE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --verbose)
      VERBOSE=1
      shift
      ;;
    --jobs)
      JOBS="${2:?--jobs requires a number}"
      shift 2
      ;;
    --tail)
      TAIL_LINES="${2:?--tail requires a number}"
      shift 2
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      REPO_ROOT="$1"
      shift
      ;;
  esac
done

if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]]; then
  for p in /mnt/mp "/media/${USER}/MelonPrimeDS" /media/sf_MelonPrimeDS; do
    [[ -f "$p/CMakeLists.txt" ]] && REPO_ROOT="$p" && break
  done
fi

if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]]; then
  echo "[guest-build-existing] Repo not found. Mount shared folder first." >&2
  exit 1
fi

BUILD_DIR="$REPO_ROOT/build-linux"
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  echo "[guest-build-existing] Missing ${BUILD_DIR}/build.ninja" >&2
  echo "[guest-build-existing] Run guest-build-only.sh once to configure the build tree." >&2
  exit 1
fi

echo "[guest-build-existing] Repo: ${REPO_ROOT}"
echo "[guest-build-existing] Build dir: ${BUILD_DIR}"
echo "[guest-build-existing] Jobs: ${JOBS}"
echo "[guest-build-existing] Skipping CMake configure."

cd "$REPO_ROOT"

if [[ "$VERBOSE" == 1 ]]; then
  cmake --build build-linux --parallel "$JOBS" --verbose
else
  set -o pipefail
  cmake --build build-linux --parallel "$JOBS" 2>&1 | tail -n "$TAIL_LINES"
fi

echo "[guest-build-existing] Build succeeded."
echo "Run: ${BUILD_DIR}/melonPrimeDS"
