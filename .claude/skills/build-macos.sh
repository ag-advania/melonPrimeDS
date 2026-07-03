#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: .claude/skills/build-macos.sh [--jobs N] [--build-only] [--release] [--open]

Default: configure build-mac (Release, MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON)
and build with Ninja. Output: build-mac/melonPrimeDS.app

Options:
  --jobs N       Parallel build jobs (default: 4)
  --build-only   Skip cmake configure; require an existing build-mac tree
  --release      MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF (distribution build)
  --open         Launch the app bundle after a successful build
  -h, --help     Show this help
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
JOBS=4
CONFIGURE=1
DEV_FEATURES=ON
OPEN_APP=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --jobs)
            [[ $# -ge 2 ]] || { echo "[melonprime-build] --jobs requires a value" >&2; exit 2; }
            JOBS="$2"
            shift 2
            ;;
        --build-only)
            CONFIGURE=0
            shift
            ;;
        --release)
            DEV_FEATURES=OFF
            shift
            ;;
        --open)
            OPEN_APP=1
            shift
            ;;
        *)
            echo "[melonprime-build] Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! command -v brew >/dev/null 2>&1; then
    echo "[melonprime-build] Homebrew is required but was not found in PATH." >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "[melonprime-build] cmake is required but was not found in PATH." >&2
    exit 1
fi

QT_PREFIX="$(brew --prefix qt)"
LIBARCHIVE_PREFIX="$(brew --prefix libarchive)"
PREFIX_PATH="${QT_PREFIX};${LIBARCHIVE_PREFIX}"

echo "[melonprime-build] Repo: $REPO_ROOT"
echo "[melonprime-build] Build dir: build-mac"
echo "[melonprime-build] Developer features: $DEV_FEATURES"
echo "[melonprime-build] Jobs: $JOBS"

cd "$REPO_ROOT"

if [[ "$CONFIGURE" -eq 1 ]]; then
    cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DMELONPRIME_ENABLE_DEVELOPER_FEATURES="$DEV_FEATURES" \
        -DCMAKE_PREFIX_PATH="$PREFIX_PATH" \
        -DUSE_QT6=ON
else
    if [[ ! -f build-mac/build.ninja ]]; then
        echo "[melonprime-build] Missing build-mac/build.ninja" >&2
        echo "[melonprime-build] Run without --build-only to configure first." >&2
        exit 1
    fi
fi

cmake --build build-mac --parallel "$JOBS" 2>&1

APP_BUNDLE="$REPO_ROOT/build-mac/melonPrimeDS.app"
echo "[melonprime-build] Bundle: $APP_BUNDLE"

if [[ "$OPEN_APP" -eq 1 ]]; then
    open "$APP_BUNDLE"
fi
