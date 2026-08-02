# macOS Build

Use this when an AI agent needs to build MelonPrimeDS on macOS and should not reconstruct the command by hand.

## Dev Build (canonical)

Fixed dev configure + build. Run from anywhere:

```zsh
./tools/build/macos/build-macos-dev.sh
```

Finder / double-click (macOS):

```zsh
open tools/build/macos/build-macos-dev.command
```

Incremental rebuild only (existing `build-mac` tree):

```zsh
open tools/build/macos/build-macos-dev-existing.command
```

This is the checked-in form of:

```zsh
cd /Users/admin/git/melonPrimeDS && cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" -DUSE_QT6=ON && cmake --build build-mac --parallel 4 2>&1
```

The script resolves the repo root from its own path instead of hard-coding the path above.

Build-only when `build-mac` is already configured:

```zsh
./tools/build/macos/build-macos-dev-existing.sh
```

Raw equivalent:

```zsh
cd /Users/admin/git/melonPrimeDS && cmake --build build-mac --parallel 4 2>&1
```

## Script Index

Every macOS build/test entry point, including the Metal test scripts, is
listed in [`tools/build/macos/README.md`](../../../tools/build/macos/README.md).

## Options Wrapper

For non-default jobs, release builds, or `--open`:

```zsh
./tools/build/macos/build-macos.sh
./tools/build/macos/build-macos.sh --jobs 8
./tools/build/macos/build-macos.sh --release
./tools/build/macos/build-macos.sh --build-only
./tools/build/macos/build-macos.sh --open
```

Prefer `build-macos-dev.sh` for the normal local dev build unless the user asks for another configuration.

## Vulkan (MoltenVK)

The Vulkan renderer builds alongside the native Metal renderer. It has its own
script and dependencies; see [`macos-vulkan.md`](macos-vulkan.md).

Metal + Vulkan in one bundle (Finder double-click):

```zsh
open tools/build/macos/build_macos_metal_n_vulkan.command
```

```zsh
./tools/build/macos/build-macos-vulkan.sh --install-deps --with-metal
```

`MELONPRIME_ENABLE_VULKAN` defaults to `ON`, so the scripts above also compile
the Vulkan renderer in once `vulkan-headers` is installed. Without those
headers CMake reports `MelonPrime Vulkan backend: disabled` and the build
continues without it.

## Dependencies

Install Homebrew packages once:

```zsh
brew install cmake ninja pkgconf sdl2 qt libarchive enet zstd faad2 libslirp
```

For the Vulkan renderer, additionally:

```zsh
brew install vulkan-headers molten-vk
```

Use Homebrew dependencies directly; do not use vcpkg for the local macOS build.

## Output

`build-mac/melonPrimeDS.app`

Launch:

```zsh
open build-mac/melonPrimeDS.app
```

## GitHub Actions

CI uses `.github/workflows/build-macos.yml` with
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF`.
