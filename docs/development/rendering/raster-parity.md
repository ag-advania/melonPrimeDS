# Raster parity verification

Vulkan and DirectX 12 use the same DS scanline rules as the Software renderer,
but shader compilation alone cannot prove that their native 3D pixels match.
The repository therefore has two complementary executable checks.

The corresponding OpenGL Compute edge changes are selected only when
`MELONPRIME_DS` is defined. The non-MelonPrime branch retains the upstream
melonDS span setup and shader interpolation so future upstream updates can be
merged without silently changing their renderer contract.

## GPU-independent edge vectors

`melonprime_raster_edge_vectors` executes the canonical edge helpers mirrored
from Software and used by the MelonPrime CPU setup stages of OpenGL Compute,
Vulkan and DX12. Software itself remains the untouched upstream reference. It
covers the ordinary/right-at-zero/coincident vertical cases, the one-scanline
vertical slope exception, the Software interpolation origin used by 45-degree
and X-major edges, swapped vertical AA coverage and the non-flat bottom X-major
condition. The target is excluded from normal builds and requested explicitly
by Windows and Ubuntu CI.

## Native 1x pixel differential

[`run-raster-differential.ps1`](../../../tools/testing/run-raster-differential.ps1)
starts a normal Release executable with `MELONPRIME_RASTER_DIFFERENTIAL=1`,
optionally sends a caller-supplied savestate hotkey, and compares all 49,152
native 3D output words from Vulkan or DX12 against a retained
`SoftRenderer3D` fed the same frame input. It fails when any word differs or
when the run never produces a non-empty 3D frame.

```powershell
tools/testing/run-raster-differential.ps1 `
  -Renderer DX12 `
  -Executable build/release-mingw-x86_64/melonPrimeDS.exe `
  -RomPath 'C:\path\game.nds'
```

Use `-LoadSlot 1` through `-LoadSlot 8` when a configured frontend hotkey and
matching state are available. Run the command again with `-Renderer Vulkan`.
The diagnostic is completely dormant when its environment variable is absent:
the Software reference is not retained, rendered or compared during ordinary
gameplay.

The native 3D comparison is intentionally separate from high-resolution
extension testing. Exact Software parity is a 1x contract; 2x–16x output is
verified for consistency and presentation correctness rather than compared
word-for-word with a renderer that has no equivalent high-resolution surface.
