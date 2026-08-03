# macOS Build

Use this when an AI agent needs to build MelonPrimeDS on macOS and should not reconstruct the command by hand.

## Default build: Metal + Vulkan (canonical for AI builds)

Builds both the native Metal renderer and the Vulkan (MoltenVK) renderer into
one bundle. They coexist: Settings -> Video -> 3D renderer lists Metal, Metal
Compute, and Vulkan as separate choices, each with its own video settings, and
picking one never changes the others. `libMoltenVK.dylib` is bundled into the
app so Vulkan runs without a system-wide install.

Build dir: `build-mac-vulkan`. Output: `build-mac-vulkan/melonPrimeDS.app`.

Run from anywhere:

```zsh
./tools/build/macos/build-macos-vulkan.sh --install-deps --with-metal
```

Finder / double-click:

```zsh
open tools/build/macos/build_macos_metal_n_vulkan.command
```

Incremental rebuild only (existing configured `build-mac-vulkan` tree; skip
this if CMake options, dependencies, or the toolchain changed):

```zsh
open tools/build/macos/build_macos_metal_n_vulkan_existing.command
```

```zsh
./tools/build/macos/build-macos-vulkan.sh --build-only --with-metal
```

`--help` on `build-macos-vulkan.sh` lists every option (`--jobs`, `--release`,
`--debug`, `--no-bundle`, `--open`). `MELONPRIME_ENABLE_VULKAN` defaults to
`ON`; without `vulkan-headers` installed, CMake reports
`MelonPrime Vulkan backend: disabled` and the build continues Metal-only.

Launch:

```zsh
open build-mac-vulkan/melonPrimeDS.app
```

## Other build configurations (non-default — use only if explicitly requested)

These are isolated from the default flow above: use them only when the user
asks for that specific configuration (a Metal-only tree, Metal renderer
self-tests, or a release/distribution build), not as a substitute for the
default build.

### Metal-only dev build → `build-mac/`

No Vulkan bundling; native Metal is still on by default on Apple platforms.

```zsh
./tools/build/macos/build-macos-dev.sh
```

Finder / double-click:

```zsh
open tools/build/macos/build-macos-dev.command
```

Incremental rebuild only (existing `build-mac` tree):

```zsh
open tools/build/macos/build-macos-dev-existing.command
```

```zsh
./tools/build/macos/build-macos-dev-existing.sh
```

Options wrapper (non-default jobs, release builds, `--open`):

```zsh
./tools/build/macos/build-macos.sh
./tools/build/macos/build-macos.sh --jobs 8
./tools/build/macos/build-macos.sh --release
./tools/build/macos/build-macos.sh --build-only
./tools/build/macos/build-macos.sh --open
```

### Metal renderer self-tests → `build-mac-metal-test/`

```zsh
open tools/build/macos/build_metal_test.command
open tools/build/macos/run_metal_test.command
```

### Vulkan-only build

`build-macos-vulkan.sh` without `--with-metal` still compiles Metal in (it is
the macOS default); the flag only adds an explicit check that both renderers
ended up in the build. There is no separate Vulkan-without-Metal entry point.

## Script Index

Every macOS build/test entry point is listed in
[`tools/build/macos/README.md`](../../../tools/build/macos/README.md).

## Vulkan (MoltenVK) details

See [`macos-vulkan.md`](macos-vulkan.md) for dependency and runtime-loader
details behind the default build above.

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

## GitHub Actions

CI uses `.github/workflows/build-macos.yml` with
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF`.
