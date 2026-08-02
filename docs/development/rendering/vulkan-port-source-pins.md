# Vulkan port source pins

The MelonPrime Vulkan port is derived from the following immutable sources.

| Role | Repository | Ref | Commit |
|---|---|---|---|
| Android frontend | `SapphireRhodonite/melonDS-android` | `0.7.0.rc5` | `9b28076281545a1e08dccee0b3f925febb8933ac` |
| Core | `SapphireRhodonite/melonDS-android-lib` | explicit MelonPrime pin | `d77944275fa61f9b79cfcead2c3e98993429a023` |
| MelonPrime target | `ag-advania/melonPrimeDS` | `develop_vulkan` starting point | `db87eb30f6de6285828dadcb06f121033dc40d47` |

The core source was verified directly from the requested standalone checkout:

```text
d77944275fa61f9b79cfcead2c3e98993429a023
```

## Baseline

Before the Vulkan import, `tools/build/windows/build-mingw-existing.bat --jobs 1`
completed successfully on Windows/MinGW with Vulkan disabled. This establishes
the compile/link baseline for the existing Software and OpenGL paths; no ROM
runtime or screenshot baseline was available in the automated workspace.

## Porting boundary

Pinned Vulkan algorithms and generated SPIR-V are copied from the commits
above. Android window, loader, custom-driver, and Hardware Buffer dependencies
are replaced by desktop equivalents. The desktop boundary also retains the
video-settings and custom-HUD integration, and presents CPU software output
outside `MelonPrimeCore::IsInGame()`; Vulkan rendering and presentation are
used only while that match-state flag is active. Both renderers continue to
honor the configured threaded-rendering value.
Shared hooks remain under the MelonPrime and Vulkan build guards.
