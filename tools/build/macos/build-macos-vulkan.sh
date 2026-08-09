#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/build/macos/build-macos-vulkan.sh [options]

Configures and builds the macOS app bundle with the MelonPrime Vulkan renderer
(MoltenVK) enabled alongside the native Metal renderer. Both appear in
Settings -> Video as separate 3D renderer choices; neither replaces the other.

Default build dir: build-mac-vulkan   Output: build-mac-vulkan/melonPrimeDS.app

Options:
  --jobs N          Parallel build jobs (default: 4)
  --build-dir DIR   Build directory (default: build-mac-vulkan)
  --build-only      Skip cmake configure; require an existing configured tree
  --release         MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF (distribution build)
  --debug           CMAKE_BUILD_TYPE=Debug (also enables Vulkan validation layers)
  --install-deps    brew install the missing Vulkan dependencies automatically
  --with-metal      Require the native Metal renderer too, and verify both
                    renderers ended up in the build (Metal is already the
                    macOS default; this makes it explicit and checked)
  --no-bundle       Do not copy libMoltenVK.dylib into the app bundle
  --open            Launch the app bundle after a successful build
  -h, --help        Show this help

Runtime dependencies (Homebrew):
  vulkan-headers    build-time only: the Vulkan API headers
  molten-vk         the Vulkan-on-Metal driver; required to actually run Vulkan
  vulkan-loader     optional: the Khronos loader, needed for validation layers

By default libMoltenVK.dylib is copied into
<bundle>/Contents/Frameworks so the bundle runs Vulkan on Macs without
Homebrew. The runtime loader search order is documented in
docs/development/build/macos-vulkan.md.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
JOBS=4
BUILD_DIR="build-mac-vulkan"
CONFIGURE=1
DEV_FEATURES=ON
BUILD_TYPE=Release
INSTALL_DEPS=0
BUNDLE_MOLTENVK=1
OPEN_APP=0
WITH_METAL=0

log() { echo "[melonprime-build] $*"; }
die() { echo "[melonprime-build] $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --jobs)
            [[ $# -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"; shift 2 ;;
        --build-dir)
            [[ $# -ge 2 ]] || die "--build-dir requires a value"
            BUILD_DIR="$2"; shift 2 ;;
        --build-only) CONFIGURE=0; shift ;;
        --release) DEV_FEATURES=OFF; shift ;;
        --debug) BUILD_TYPE=Debug; shift ;;
        --install-deps) INSTALL_DEPS=1; shift ;;
        --with-metal) WITH_METAL=1; shift ;;
        --no-bundle) BUNDLE_MOLTENVK=0; shift ;;
        --open) OPEN_APP=1; shift ;;
        *) echo "[melonprime-build] Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ "$(uname -s)" == "Darwin" ]] || die "This script only runs on macOS."
command -v brew >/dev/null 2>&1 || die "Homebrew is required but was not found in PATH."
command -v cmake >/dev/null 2>&1 || die "cmake is required but was not found in PATH."

BREW_PREFIX="$(brew --prefix)"

# --- Vulkan dependencies ----------------------------------------------------
# vulkan-headers is the only build-time requirement. molten-vk is what makes
# the Vulkan renderer selectable at runtime, so a build without it is reported
# as such rather than silently producing an app whose Vulkan option is greyed
# out.
MISSING_REQUIRED=()
brew list --formula vulkan-headers >/dev/null 2>&1 || MISSING_REQUIRED+=(vulkan-headers)
brew list --formula molten-vk >/dev/null 2>&1 || MISSING_REQUIRED+=(molten-vk)

if [[ ${#MISSING_REQUIRED[@]} -gt 0 ]]; then
    if [[ "$INSTALL_DEPS" -eq 1 ]]; then
        log "Installing missing Vulkan dependencies: ${MISSING_REQUIRED[*]}"
        brew install "${MISSING_REQUIRED[@]}"
    else
        echo "[melonprime-build] Missing Vulkan dependencies: ${MISSING_REQUIRED[*]}" >&2
        echo "[melonprime-build] Install them with:" >&2
        echo "    brew install ${MISSING_REQUIRED[*]}" >&2
        echo "[melonprime-build] Or re-run this script with --install-deps." >&2
        exit 1
    fi
fi

VULKAN_HEADERS_PREFIX="$(brew --prefix vulkan-headers)"
MOLTENVK_PREFIX="$(brew --prefix molten-vk)"
MOLTENVK_DYLIB="$MOLTENVK_PREFIX/lib/libMoltenVK.dylib"
[[ -f "$MOLTENVK_DYLIB" ]] || die "libMoltenVK.dylib was not found at $MOLTENVK_DYLIB"

QT_PREFIX="$(brew --prefix qt)"
LIBARCHIVE_PREFIX="$(brew --prefix libarchive)"
PREFIX_PATH="${QT_PREFIX};${LIBARCHIVE_PREFIX};${VULKAN_HEADERS_PREFIX}"

log "Repo: $REPO_ROOT"
log "Build dir: $BUILD_DIR ($BUILD_TYPE)"
log "Developer features: $DEV_FEATURES"
log "Vulkan headers: $VULKAN_HEADERS_PREFIX"
log "MoltenVK: $MOLTENVK_DYLIB"
log "Jobs: $JOBS"

cd "$REPO_ROOT"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DMELONPRIME_ENABLE_DEVELOPER_FEATURES="$DEV_FEATURES"
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH"
    -DUSE_QT6=ON
    -DMELONPRIME_ENABLE_VULKAN=ON
    -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF
)
if [[ "$BUNDLE_MOLTENVK" -eq 1 ]]; then
    CMAKE_ARGS+=(
        -DMELONPRIME_BUNDLE_MOLTENVK=ON
        -DMELONPRIME_MOLTENVK_DYLIB="$MOLTENVK_DYLIB"
    )
else
    CMAKE_ARGS+=(
        -DMELONPRIME_BUNDLE_MOLTENVK=OFF
    )
fi
if [[ "$WITH_METAL" -eq 1 ]]; then
    # Metal is already the macOS default; setting it explicitly keeps the
    # "both renderers" intent visible in the cache and in this log.
    CMAKE_ARGS+=(
        -DMELONPRIME_ENABLE_METAL=ON
        -DMELONPRIME_FORCE_DISABLE_METAL=OFF
    )
    log "Renderers: Metal + Vulkan"
else
    log "Renderers: Vulkan (Metal follows the macOS default, which is ON)"
fi

if [[ "$CONFIGURE" -eq 1 ]]; then
    cmake -B "$BUILD_DIR" -G Ninja "${CMAKE_ARGS[@]}"
else
    [[ -f "$BUILD_DIR/build.ninja" ]] \
        || die "Missing $BUILD_DIR/build.ninja; run without --build-only to configure first."
fi

# The renderer gates are resolved at configure time. Fail loudly rather than
# shipping a bundle whose renderer was silently compiled out.
# find_path() records a NOTFOUND sentinel rather than omitting the entry, so
# match on a real path.
if ! grep -Eq '^MELONPRIME_VULKAN_INCLUDE_DIR:PATH=/' "$BUILD_DIR/CMakeCache.txt"; then
    die "CMake did not find the Vulkan headers; the Vulkan renderer would be excluded."
fi
if [[ "$WITH_METAL" -eq 1 ]]; then
    if grep -q '^MELONPRIME_FORCE_DISABLE_METAL:BOOL=ON' "$BUILD_DIR/CMakeCache.txt" \
        || ! grep -q '^MELONPRIME_ENABLE_METAL:BOOL=ON' "$BUILD_DIR/CMakeCache.txt"; then
        die "Metal was requested but is disabled in $BUILD_DIR/CMakeCache.txt."
    fi
fi

cmake --build "$BUILD_DIR" --parallel "$JOBS" 2>&1

APP_BUNDLE="$REPO_ROOT/$BUILD_DIR/melonPrimeDS.app"
[[ -d "$APP_BUNDLE" ]] || die "Build finished but $APP_BUNDLE does not exist."

if [[ "$BUNDLE_MOLTENVK" -eq 1 ]]; then
    FRAMEWORKS_DIR="$APP_BUNDLE/Contents/Frameworks"
    [[ -f "$FRAMEWORKS_DIR/libMoltenVK.dylib" ]] \
        || die "CMake did not bundle MoltenVK at $FRAMEWORKS_DIR/libMoltenVK.dylib"
    log "Bundled MoltenVK: $FRAMEWORKS_DIR/libMoltenVK.dylib"
fi

log "Bundle: $APP_BUNDLE"
log "Select the renderer in Settings -> Video -> 3D renderer -> Vulkan."

if [[ "$OPEN_APP" -eq 1 ]]; then
    open "$APP_BUNDLE"
fi
