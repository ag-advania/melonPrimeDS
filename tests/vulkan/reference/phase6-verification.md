# Vulkan Phase 6 verification

**Status:** Patch applied; Windows build pending.
**Build status:** `PENDING_WINDOWS_BUILD`
**ROM runtime status:** `PENDING_ROM_RUNTIME`

## Implemented scope

- Adds `melonDS::VulkanRenderer` under `MELONPRIME_VULKAN_ACTIVE` only.
- Keeps Software 2D, Software 3D, display capture, savestate hooks, and CPU BGRA output as the correctness source.
- Routes `renderer3D_Vulkan` and `renderer3D_VulkanCompute` through the renderer factory instead of replacing them with `SoftRenderer` before construction.
- Preserves requested/normalized/actual backend logging.
- Records frame serial and output generation on CPU BGRA output.
- Advances generation on reset, stop, savestate load completion, and scale changes.
- Adds `--melonprime-vulkan-renderer-shell-test <json>` for a no-ROM link/contract check.
- Does not claim native Vulkan 3D; that begins in Phase 7.

## Windows commands executed by the package

```bat
.claude\skills\build-mingw.bat --vulkan --tail 160
build\release-mingw-x86_64\melonPrimeDS.exe --melonprime-vulkan-renderer-shell-test <json>
build\release-mingw-x86_64\melonPrimeDS.exe --melonprime-output-lease-test <json>
build\release-mingw-x86_64\melonPrimeDS.exe --melonprime-vulkan-probe <json>
```

## Expected shell harness contract

```json
{
  "passed": true,
  "contract_version": 1,
  "raster_software_correctness_baseline": true,
  "compute_software_correctness_baseline": true,
  "raster_native_vulkan_3d": false,
  "compute_native_vulkan_3d": false
}
```

## Required ROM runtime follow-up

Run with a valid ROM and the Phase 4 Vulkan presenter:

```bat
set MELONPRIME_FORCE_VULKAN_RENDERER=1
set MELONPRIME_FORCE_VULKAN_PRESENTER=1
build\release-mingw-x86_64\melonPrimeDS.exe "X:\path\game.nds"
```

Confirm the log contains `actual=Vulkan(5)` and the explicit Software correctness-baseline shell log. Repeat with `MELONPRIME_FORCE_VULKAN_COMPUTE_RENDERER=1` and confirm `actual=VulkanCompute(6)`.

## Deferred acceptance

- ROM boot, reset, savestate load, renderer switch, and long-run gameplay require the user's ROM environment.
- Native Vulkan rasterization is intentionally absent.
- Linux, macOS/MoltenVK, CI, and validation-layer execution remain deferred.
