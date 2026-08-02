# macOS Vulkan Build (MoltenVK)

How to build and run the MelonPrime Vulkan renderer on macOS. For the plain
macOS build see [`macos.md`](macos.md).

Vulkan and the native Metal renderer coexist. They are independent build gates
(`MELONPRIME_ENABLE_VULKAN`, `MELONPRIME_ENABLE_METAL`), independent screen
panels, and separate entries in *Settings → Video → 3D renderer*. Selecting one
never changes the other's settings; the persisted `3D.Renderer` value keeps
Metal, Metal Compute, and Vulkan as distinct IDs.

## Build

Metal + Vulkan in one bundle (Finder double-click, installs missing Homebrew
dependencies automatically):

```zsh
open tools/build/macos/build_macos_metal_n_vulkan.command
```

Incremental rebuild of an already configured tree:

```zsh
open tools/build/macos/build_macos_metal_n_vulkan_existing.command
```

The same thing from a shell, which is also where the options live:

```zsh
./tools/build/macos/build-macos-vulkan.sh --install-deps --with-metal
```

Output: `build-mac-vulkan/melonPrimeDS.app`

Every macOS build script is indexed in
[`tools/build/macos/README.md`](../../../tools/build/macos/README.md).

Useful options: `--jobs N`, `--release` (developer features off), `--debug`
(enables the Vulkan validation layer, which also needs `vulkan-loader`),
`--with-metal`, `--no-bundle`, `--open`. `--help` lists them all.

The script fails instead of silently producing a build without the renderer it
promised: after configuring it checks that CMake resolved the Vulkan headers,
and with `--with-metal` that the Metal gate is on.

## Dependencies

```zsh
brew install vulkan-headers molten-vk
```

| Package | Needed for |
| --- | --- |
| `vulkan-headers` | build time only — the Vulkan API headers |
| `molten-vk` | runtime — the Vulkan-on-Metal driver |
| `vulkan-loader` | optional — the Khronos loader; required for validation layers |

Nothing Vulkan is linked at build time. `VulkanDispatch` opens the driver with
`dlopen()` at startup, so a build made on a machine with the headers still runs
on a machine without them.

## Runtime loader search order

`VulkanDispatch::Initialize()` tries, in order:

1. `$MELONPRIME_VULKAN_LOADER` (explicit override, any platform)
2. `$VULKAN_SDK/lib/libvulkan.1.dylib`, then `$VULKAN_SDK/lib/libMoltenVK.dylib`
3. `@executable_path/../Frameworks/libvulkan.1.dylib` (a loader bundled in the app)
4. `libvulkan.1.dylib`, `libvulkan.dylib` (default dyld search)
5. `/opt/homebrew/lib/libvulkan.1.dylib`, `/usr/local/lib/libvulkan.1.dylib`
6. `@executable_path/../Frameworks/libMoltenVK.dylib` (the bundled driver)
7. `libMoltenVK.dylib`, then the same two Homebrew prefixes

The Homebrew prefixes are listed explicitly because `dlopen()` by bare name
searches `/usr/local/lib` but not `/opt/homebrew/lib`, so Apple Silicon
installs are otherwise invisible.

The bundled MoltenVK sits *after* every loader candidate on purpose. It makes
the app self-contained, but an installed Khronos loader still wins, so
validation layers stay reachable in a `--debug` build.

A candidate is rejected and the search continues when it exposes no
`VK_EXT_metal_surface`. That is what a Khronos loader with no registered ICD
looks like, and skipping it lets the MoltenVK candidates further down the list
still succeed instead of failing much later at surface creation.

By default the build script copies `libMoltenVK.dylib` into
`melonPrimeDS.app/Contents/Frameworks` (candidate 6) and re-signs the bundle,
so the app runs Vulkan on Macs without Homebrew. Pass `--no-bundle` to skip it.

## How presentation works

MoltenVK can only present to a `CAMetalLayer`, so the panel hosts one and
passes it to `vkCreateMetalSurfaceEXT`.

- `MelonPrimeVulkanSurfaceMacOS.mm` owns the layer and the surface adapter.
- `MelonPrimeVulkanSurfaceMacOS.h` exposes the plain-C++ half so `Screen.cpp`
  needs no Objective-C.
- The layer is created and re-hosted only on the GUI thread
  (`ScreenPanelVulkan::refreshNativeSurfaceGuiThread()`), which is why
  `setupScreenLayout()` does not touch it — `initVulkanPresenter()` calls that
  from the emulation thread.
- Qt rebuilds the native view on fullscreen and screen changes. The panel
  re-hosts the *same* layer rather than making a new one, so the existing
  `VkSurfaceKHR` stays valid.
- If the layer is not ready when the emulation thread wants to present, the
  frame is skipped and a GUI-thread refresh is queued. This is deliberately not
  treated as a hard runtime failure, so a transient view rebuild does not
  permanently drop the session back to software rendering.
- One frame of MoltenVK work runs inside an `@autoreleasepool`. The emulation
  thread has no run loop, so the autoreleased Metal objects MoltenVK returns
  would otherwise accumulate for the whole session.

### Why the layer is a sublayer

On Windows and Linux `ScreenPanelVulkan` sets `WA_PaintOnScreen` and presents
into its own window handle. That does not work on macOS: Qt's macOS backend has
no on-screen paint support, so `QWidget::paintEngine()` returns null and
`QPainter` silently fails (`QPainter::begin: Paint device returned engine == 0`).

The panel needs `QPainter`. Vulkan runs the **software** renderer for the
splash screen and for everything outside a match, and those frames — plus the
OSD — are drawn with `QPainter` in `paintEvent()`. Making the panel's view
host a `CAMetalLayer` would leave all non-match screens blank.

So on macOS only:

- the panel stays a normal Qt-painted widget (no `WA_PaintOnScreen`), keeping
  the splash, software screens, HUD, and OSD working unchanged;
- the `CAMetalLayer` is added as a **sublayer** of the view's existing backing
  layer, so Vulkan composites above Qt's drawing without replacing it;
- the sublayer is unhidden only after a frame has actually reached the
  swapchain, and hidden again whenever the renderer falls back to the software
  output, so the two never draw over each other.

Windows and Linux keep the single-widget, layer-free path exactly as before.

A native child `QWidget` was tried first and rejected. A second `NSView` takes
part in AppKit hit testing, and AppKit can deliver a mouse event to it after
its `QPlatformWindow` is gone — that crashed on teardown in
`-[QNSView(MouseAPI) handleMouseEvent:]`. The sublayer keeps exactly one
`NSView`, so that class of problem cannot occur.

`VulkanContext` already enabled `VK_EXT_metal_surface`,
`VK_KHR_portability_enumeration`, and `VK_KHR_portability_subset` on Apple
platforms before this port; no core changes were needed there.

## Verification status

Verified on macOS 15 (Darwin 24.6), x86_64, Homebrew Qt 6, MoltenVK 1.4.2,
Intel Iris Plus 655:

- Configure + build + link + ad-hoc sign of the app bundle.
- Loader resolution: `VulkanDriver: active=system path=/usr/local/lib/libvulkan.1.dylib`.
- Device selection through MoltenVK, `MelonPrime Vulkan probe: available=1`.
- `VulkanPresenter: surface=Metal extension=VK_EXT_metal_surface presentSupport=1`.
- Swapchain creation (`images=3`), presentation, and `OUT_OF_DATE` recreation
  after a window resize.
- The Vulkan 3D renderer rasterizing real frames from the ROM
  (`VulkanGraphics[Triangles] ... VulkanGraphics[Draws]`).
- No `QPainter`/paint-engine warnings, and no runtime fallback to software.

Not verified:

- On-screen pixel output was not visually inspected, so image orientation,
  scaling, and the software/Vulkan handover are confirmed only at the API and
  log level. Check these first when eyeballing a build.
- Apple Silicon (arm64). The code is architecture-neutral, but no arm64 run was
  made.
- Gameplay inside a match, controller/aim behaviour, and long-session stability.

Windows remains the tuned gameplay target; macOS Vulkan is a supported build
and runtime target, not a validated-parity one. MoltenVK translates SPIR-V to
Metal, so compute-heavy renderer paths can differ from a native Vulkan driver.
Report visual differences against the Metal renderer rather than assuming
parity.
