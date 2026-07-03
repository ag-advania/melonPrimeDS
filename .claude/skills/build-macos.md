# macOS Build

Use this when building or launch-testing MelonPrimeDS natively on macOS.

## Dependencies

Install Homebrew packages once:

```zsh
brew install cmake ninja pkgconf sdl2 qt libarchive enet zstd faad2 libslirp
```

Recent Homebrew installs `sdl2-compat`, which satisfies the SDL2 dependency.
Use Homebrew dependencies directly; do not use vcpkg for the local macOS build.

## Configure

From the repository root:

```zsh
cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" \
  -DUSE_QT6=ON
```

If CMake input files or bundle metadata changed, rerun the configure command
before building. The app bundle is generated at `build-mac/melonPrimeDS.app`.
By default, CMake ad-hoc signs the bundle after each successful macOS build
(`MACOS_ADHOC_SIGN_BUNDLE=ON`). This stabilizes local/CI bundle identity but is
not Developer ID notarization, so Gatekeeper may still warn on downloaded
release zips.

## Build

```zsh
cmake --build build-mac --parallel 4
```

For a concise status check:

```zsh
cmake --build build-mac --parallel 4
plutil -p build-mac/melonPrimeDS.app/Contents/Info.plist | grep CFBundleExecutable
ls -la build-mac/melonPrimeDS.app/Contents/MacOS/
codesign --verify --deep --strict build-mac/melonPrimeDS.app
```

`CFBundleExecutable` must match the executable in `Contents/MacOS`:
`melonPrimeDS`.

## Launch Check

Open the bundle from Finder or Terminal:

```zsh
open build-mac/melonPrimeDS.app
```

When testing from an AI/sandboxed environment, launching GUI apps may require
approval. If the app bounces and exits immediately, first check:

```zsh
plutil -p build-mac/melonPrimeDS.app/Contents/Info.plist
otool -L build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS
```

The most common local bundle issue is `CFBundleExecutable` pointing at the old
upstream name `melonDS` while the actual binary is `melonPrimeDS`.

## Optional Self-Contained Bundle

The normal local build links to Homebrew libraries. For a bundle that can run on
another Mac without Homebrew dependencies:

```zsh
cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" \
  -DUSE_QT6=ON -DMACOS_BUNDLE_LIBS=ON
cmake --build build-mac --parallel 4
```

The post-build bundling step uses `tools/mac-libs.rb`.

## GitHub Actions

The CI workflow is `.github/workflows/build-macos.yml`, adapted from upstream
melonDS for the MelonPrimeDS bundle name. It builds both existing macOS presets:

- `release-mac-x86_64`
- `release-mac-arm64`

The architecture-specific artifacts are zipped as `melonPrimeDS.app`, then the
workflow combines them with `lipo` into a universal `melonPrimeDS.app`.
Per-arch bundles are ad-hoc signed in CMake (`MACOS_ADHOC_SIGN_BUNDLE`, with
`codesign --force`); the universal job re-signs with `--force` after `lipo`
because replacing the main binary invalidates the copied arm64 signature.
All bundles are verified with `codesign --verify --deep --strict` before upload.
CI explicitly configures release artifacts with
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF`.

If the bundle name or executable name changes, update all of these together:

- `src/frontend/qt_sdl/CMakeLists.txt` `OUTPUT_NAME`
- `res/melon.plist.in` `CFBundleExecutable`
- `.github/workflows/build-macos.yml` bundle paths and `lipo` target
