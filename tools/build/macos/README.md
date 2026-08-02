# macOS build scripts

All macOS build and test entry points live here. `.command` files are
double-clickable in Finder; `.sh` files are for the shell.

Full procedures: [`docs/development/build/macos.md`](../../../docs/development/build/macos.md)
and [`docs/development/build/macos-vulkan.md`](../../../docs/development/build/macos-vulkan.md).

## Standard build → `build-mac/`

| Script | Purpose |
| --- | --- |
| `build-macos-dev.sh` / `.command` | Canonical dev build (Release, developer features ON). Start here. |
| `build-macos-dev-existing.sh` / `.command` | Incremental rebuild of a configured `build-mac/`. |
| `build-macos.sh` | Options wrapper: `--jobs N`, `--release`, `--build-only`, `--open`. |

## Metal + Vulkan build → `build-mac-vulkan/`

Builds both renderers into one bundle. They coexist as separate entries under
*Settings → Video → 3D renderer*, each with its own video settings.

| Script | Purpose |
| --- | --- |
| `build_macos_metal_n_vulkan.command` | Double-click entry point. Installs missing Homebrew Vulkan dependencies, configures, builds, bundles MoltenVK, re-signs. |
| `build_macos_metal_n_vulkan_existing.command` | Incremental rebuild of a configured `build-mac-vulkan/`. |
| `build-macos-vulkan.sh` | The script both wrappers call. `--help` lists every option (`--jobs`, `--release`, `--debug`, `--with-metal`, `--no-bundle`, `--open`). |

## Metal renderer testing → `build-mac-metal-test/`

| Script | Purpose |
| --- | --- |
| `build_metal_test.command` | Metal-only test build with the Metal gates set explicitly. |
| `run_metal_test.command` | Launches that build with Metal perf logging on. Takes an optional ROM path. |

## Conventions

- Each script resolves the repository root from its own path, so it can be run
  from any working directory or double-clicked from Finder.
- Build directories are created at the repository root and are gitignored by
  the `/build*/` rule; this source directory is not affected by it.
- No script writes outside its own build directory.
