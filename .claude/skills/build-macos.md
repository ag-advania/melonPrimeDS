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

## Build

```zsh
cmake --build build-mac --parallel 4
```

For a concise status check:

```zsh
cmake --build build-mac --parallel 4
plutil -p build-mac/melonPrimeDS.app/Contents/Info.plist | grep CFBundleExecutable
ls -la build-mac/melonPrimeDS.app/Contents/MacOS/
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
