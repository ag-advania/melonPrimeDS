# Vulkan port source pins

The MelonPrime Vulkan port is derived from the following immutable sources.

| Role | Repository | Ref | Commit |
|---|---|---|---|
| Android frontend | `SapphireRhodonite/melonDS-android` | `0.7.0.rc4` | `2c10e59d7209d354e90d9ef4228330bac3f6e794` |
| Core | `SapphireRhodonite/melonDS-android-lib` | frontend gitlink | `d77944275fa61f9b79cfcead2c3e98993429a023` |
| MelonPrime target | `ag-advania/melonPrimeDS` | `develop_vulkan` starting point | `db87eb30f6de6285828dadcb06f121033dc40d47` |

The source gitlink was verified with:

```text
160000 commit d77944275fa61f9b79cfcead2c3e98993429a023 melonDS-android-lib
```

## Baseline

Before the Vulkan import, `tools/build/windows/build-mingw-existing.bat --jobs 1`
completed successfully on Windows/MinGW with Vulkan disabled. This establishes
the compile/link baseline for the existing Software and OpenGL paths; no ROM
runtime or screenshot baseline was available in the automated workspace.

## Porting boundary

Pinned Vulkan algorithms and generated SPIR-V are copied from the commits
above. Android window, loader, custom-driver, and Hardware Buffer dependencies
are replaced by desktop equivalents. Shared melonDS files are not overwritten;
all shared hooks remain under the MelonPrime and Vulkan build guards.
