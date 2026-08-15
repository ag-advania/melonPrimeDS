# MelonPrimeDS Linux Vulkan 根本原因修正後監査

日付: 2026-08-15  
対象ブランチ: `develop_hud`  
実装開始時点: `231cb9b89` (`feat(hud): add Classic and Retro On-Screen Edit styles`)

## 判定

Linux/Wayland の native surface lifecycle 修正は、コード上の因果鎖と共通ビルドで確認できる状態まで実装した。Linux実機ビルド、Wayland/XCBの実画面present、GPUベンダー別runtimeはこのWindows作業環境では実行できないため、完了ではなく `OPEN / NOT RUN` とする。

BSDのWSI実装は変更していない。BSDについては、今回のLinux修正の影響範囲外として専用 `MelonPrimeVulkanSurfaceBSD.cpp` を維持する。FreeBSD/NetBSD/OpenBSDの実画面X11 presentは、この監査では再実行していないため `OPEN / P3 runtime validation gap` とする。

## 修正内容

| 根本原因／要求 | 実装結果 |
| --- | --- |
| hidden child が first present 後にしか表示されない | Linux child host は emulation-thread のpresenter bindより前にGUI threadへ `show()` を要求する。first present成功後のshow依存を削除。 |
| `WA_PaintOnScreen` のWayland適用 | Linux専用hostで `WA_PaintOnScreen` は `platformName() == "xcb"` のときだけ設定。Waylandでは `WA_NativeWindow` / `WA_NoSystemBackground` / `WA_OpaquePaintEvent` を維持。 |
| WIdだけでWayland surfaceを同一視 | `NativeWindowSnapshot::Generation` を導入し、`wl_display*` / `wl_surface*` をGUI event後にスナップショット化。WIdはXCB/Xlib専用。 |
| lifecycle eventの所有者 | child host自身が `Show` / `Hide` / `WinIdChange` / `SurfaceCreated` / `SurfaceAboutToBeDestroyed` を処理し、Qt処理後にcallbackを発火。 |
| GUIとVulkan操作の競合 | GUIはsnapshot/atomic stateだけをpublishし、presenterのsurface作成・破棄・rebindはemulation-thread frame boundaryで実行。lifecycle mutexでnative transitionとpresentを直列化。 |
| `VK_ERROR_SURFACE_LOST_KHR` の誤ったfatal化 | surface create、surface capabilities、surface formats、present modes、swapchain create、acquire、present、present-pacing queryをsurface rebind経路へ分類。`VulkanFeatureCheck::ReportRuntimeFailure`へ渡さない。 |
| lifecycleのテスト不足 | GPU-free `vulkan-linux-surface-lifecycle-tests` を追加し、hidden→show→bind、同一WIdでgeneration更新、surface lost再bind、device lost fatalを検証。 |

## 変更ファイル

- `src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHostLinux.h/.cpp`
- `src/frontend/qt_sdl/MelonPrimeVulkanSurface.h`
- `src/frontend/qt_sdl/MelonPrimeVulkanSurfaceLinux.cpp`
- `src/frontend/qt_sdl/MelonPrimeVulkanPresenter.h/.cpp`
- `src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp`
- `src/frontend/qt_sdl/Screen.h`
- `src/frontend/qt_sdl/CMakeLists.txt`
- `src/VulkanPresentPacer.h/.cpp`
- `tools/testing/vulkan-linux-surface-lifecycle-tests.cpp`

`src/frontend/qt_sdl/MelonPrimeVulkanSurfaceBSD.cpp` は差分なし。Linuxのsurface adapterとBSD/X11 adapterのCMake分岐も分離したまま維持している。

## 実行した検証

### PASS

- `git diff --check`
- Windows MinGW configured build: `build/release-mingw-x86_64`、変更後のcore/presenter/screen compile、link、custom testを含めてPASS
- `Vulkan present timing model tests PASS`
- `vulkan-linux-surface-lifecycle-tests: PASS`
- `Vulkan present pacer fake-dispatch tests passed`
- `Intel XeLL fake API state-machine tests PASS`
- `tools/ci/audits/audit-platform-scatter-budget.ps1`: `21 / 22 PASS`
- `tools/ci/audits/audit-melonprime-thread-boundary.ps1 -Strict`: `findings: 0`

GPU-freeテストはWindows上で実行したものであり、Linux Qt/QPA分岐のコンパイルや実画面presentを証明するものではない。

### NOT RUN / OPEN

- Linux build: `bash tools/build/linux/build-linux-existing.sh --jobs 1` はWindows MSYS hostから実行できず、`Unsupported host for Linux build wrapper: MSYS_NT-10.0-22621`。
- Linux Wayland runtime: NOT RUN。Wayland compositor、Qt Wayland plugin、実GPUが必要。
- Linux XCB/X11 runtime: NOT RUN。X server上のnative child、`vkCreateXcbSurfaceKHR`、swapchain、acquire、presentの実測が必要。
- Linux vendor matrix (AMD / Intel / NVIDIA): NOT RUN。Windows側の共通コンパイルや他GPUの証拠をLinux runtimeの代用にはしない。
- BSD FreeBSD / NetBSD / OpenBSD runtime X11 present: OPEN。今回BSD source/CIは再実行せず、実機画面表示の証明もない。

このため、現時点の最終判定は `IMPLEMENTED / STATIC AND COMMON BUILD PASS / LINUX RUNTIME OPEN` とする。Linux runtimeのログでは少なくとも次の順序を確認してからruntime closureへ進める。

```text
SurfaceCreated / host show
    -> VkSurfaceKHR create
    -> swapchain create
    -> Acquire
    -> first present
```

## 監査上の注意

- `QCoreApplication::processEvents()`、sleep/delay、Software/OpenGL fallback、Wayland disable、WId-only identityは追加していない。
- `VK_ERROR_SURFACE_LOST_KHR` は再bind可能な状態であり、`VK_ERROR_DEVICE_LOST` やその他の致命的結果とは分離している。
- 最終的なQt object destructionはemulation threadが到達不能になった後のterminal teardownであり、通常のHide/SurfaceAboutToBeDestroyedおよびgeneration変更によるrebindはemulation-thread側の `retireLinuxPresentationSurface()` が担当する。
