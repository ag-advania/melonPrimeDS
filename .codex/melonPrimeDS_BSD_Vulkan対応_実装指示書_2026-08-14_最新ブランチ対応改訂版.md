# melonPrimeDS BSD Vulkan対応 実装指示書

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver3`
- 初版調査基準HEAD: `19dd1bc29397cc263fc88a1255aa0621b2e819f7`
- 最新再監査HEAD: `dbf416d080093938dcb0d65cda1d1eced317aec8` (`Clarify Vulkan fallback audit provenance`)
- 初版HEADから最新HEADまで: `21 commits ahead / 0 behind`
- 作成日: 2026-08-14
- 対象: FreeBSD / OpenBSD / NetBSD
- 目的: BSD版melonPrimeDSでもVulkan rendererをビルド可能かつ利用可能な構成へ拡張する
- 重要前提: **依頼者はBSD実機環境を所持していないため、物理BSD環境・実GPUでのruntimeテストは実施できない。**
- 検証方針: GitHub Actions上のBSD VMによるcompile/build/static validationを必須とし、可能ならsoftware Vulkanによるruntime smokeを追加する。ただし物理GPUでのruntime成功はDefinition of Doneに含めない。

---


# 0. 最新ブランチ再監査結果

2026-08-14 23:43 JST時点の`develop_remakeVulkan_ver3` HEAD
`dbf416d080093938dcb0d65cda1d1eced317aec8`を、初版調査HEAD
`19dd1bc29397cc263fc88a1255aa0621b2e819f7`と比較した。

結論:

```text
BSD Vulkan対応の根本方針:
    そのまま適用可能

BSDが現在Stubへ落ちるという前提:
    変化なし

Linux Vulkan WSIが__linux__限定:
    変化なし

VulkanContextのXCB/Xlib/Wayland extension選択:
    変化なし

VulkanLoaderのUnix loader候補:
    変化なし

MD修正:
    必要
```

修正が必要な理由は、BSD Vulkanの根本設計が変わったからではなく、
**最新branch側のQt/CI/test構成が初版作成後に強化されたため**である。

今回の改訂で追加・変更する重要点:

1. 現在の`Screen.cpp`はQt 6.5以上のX11/BSD経路で既に
   `QNativeInterface::QX11Application`を使用している。Qt公式上
   `QX11Application`自体はQt 6.2から存在するが、今回のBSD Vulkan
   Phase 1は既存frontendとの分岐を揃えるため**Qt 6.5以上を正式検証範囲**
   とし、Qt 6.2～6.4へ広げる場合は別途CI確認する。
2. BSD Vulkan adapter自体はQt private QPAへ依存させない。ただし現在の
   shared `Screen.cpp`にはQt version/platform次第でprivate QPAを使う既存経路があるため、
   「BSD build全体からprivate QPA依存を完全排除する」という意味にはしない。
3. 現在の`.github/workflows/build-bsd.yml`は`build` job自体が
   `continue-on-error: true`であり、artifact集約側も部分成功を許す。
   したがって**現状workflow成功表示をそのままVulkan DoDのgating evidenceにしてはいけない。**
   BSD Vulkan validation用にはstrict jobを追加するか、対象jobから
   `continue-on-error`を外す。
4. 最新branchではVulkan ON時に
   `melonprime_vulkan_present_timing_check`および
   `melonprime_vulkan_present_pacer_dispatch_check`が`ALL` targetとして実行される。
   BSD Vulkan ON buildではこれらのPASSも必須証跡に追加する。
5. 初版後にVulkan renderer fallback/panel lifetimeが強化されているため、
   BSD WSI失敗時はその既存fallback contractを再利用し、BSD専用fallback state machineを
   新設しない。

---

# 1. 目的

現在のmelonPrimeDSのVulkan renderer本体は、Windows・Linux・macOSだけに閉じた設計ではない。

`VulkanContext.cpp`ではWindows/macOS以外について、

- `VK_KHR_xcb_surface`
- `VK_KHR_xlib_surface`
- `VK_KHR_wayland_surface`

をruntime capabilityとして扱う実装が既に存在する。

一方、BSDではfrontend側の`VkSurfaceKHR`生成処理が未実装であり、現在は

`src/frontend/qt_sdl/MelonPrimeVulkanSurfaceStub.cpp`

へ落ちる。

したがって今回の主目的は、Vulkan renderer本体を書き直すことではなく、

```text
Qt QWidget / QWindow
        ↓
BSDのX11 native connection / window ID
        ↓
VK_KHR_xcb_surface または VK_KHR_xlib_surface
        ↓
VkSurfaceKHR
        ↓
既存 MelonPrimeVulkanPresenter
```

を接続すること。

---

# 2. 現状調査結果

## 2.1 Vulkan build option

現在の

`src/frontend/qt_sdl/CMakeLists.txt`

では、

```cmake
option(MELONPRIME_ENABLE_VULKAN
    "Build the MelonPrime Vulkan renderer and native presenter"
    ON)
```

となっており、Vulkan自体はデフォルトON。

ただし、

```cmake
find_path(MELONPRIME_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    ...)
```

でVulkan headerが見つからなければ、

```text
Vulkan headers were not found; MelonPrime Vulkan is excluded from this build
```

となる。

したがってBSD workflowでもVulkan headerを必ず導入する必要がある。

---

## 2.2 BSDが現在Vulkanを使用できない直接原因

現在のCMakeはVulkan WSI adapterを以下のように選択している。

```text
Windows -> MelonPrimeVulkanSurfaceWin32.cpp
macOS   -> MelonPrimeVulkanSurfaceMacOS.mm
Linux   -> MelonPrimeVulkanSurfaceLinux.cpp
その他  -> MelonPrimeVulkanSurfaceStub.cpp
```

BSDは最後のStubへ入る。

Stubは意図的に、

```text
this platform has no Vulkan window-system integration in melonPrimeDS
```

として失敗する。

つまり、

**BSDにVulkan renderer本体が存在しないのではなく、BSD用のwindow-system integrationが未実装。**

---

## 2.3 Linux実装をBSDへそのまま広げていない理由

最新HEADでも`MelonPrimeVulkanSurfaceLinux.cpp`は、

```cpp
#include <qpa/qplatformnativeinterface.h>
```

というQt private QPA APIを使用し、translation unit全体が

```cpp
defined(__linux__)
```

に限定されている。

コードコメントにも、BSD buildへこのprivate QPA dependencyを持ち込まないため
Linux限定にしている旨が残っている。

一方、最新`Screen.cpp`ではX11 native handle取得について、

```cpp
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QX11Application* x11 =
        qApp->nativeInterface<QX11Application>();
#endif
```

というpublic native interface経路が既に存在する。

したがって今回も、

```cpp
defined(__linux__)
```

を単純に削除してLinux adapterをBSDへ流用してはいけない。

**BSD Vulkan adapterはLinux adapterのprivate-QPA実装をコピーするのではなく、
既存`Screen.cpp`のBSD/X11 public-native-interface方針へ揃える。**

---

# 3. 実装方針

## 3.1 Phase 1の対応範囲

最初のBSD Vulkan対応は**X11 WSIを正式対応範囲**とする。

対応するsurface extension:

```text
VK_KHR_xcb_surface
VK_KHR_xlib_surface
```

優先順位:

```text
1. XCB
2. Xlib fallback
```

理由:

- FreeBSD / OpenBSD / NetBSDのQt desktop buildで最も現実的な共通経路
- 現行Linux実装もXCBを第一候補としている
- Qt公式では`QNativeInterface::QX11Application`はQt 6.2から提供され、
  `connection()`と`display()`でXCB/Xlib handleを取得できる
- 最新melonPrimeDSの`Screen.cpp`はQt 6.5以上のX11/BSD経路で既に
  `QX11Application`を採用している
- private QPA headerをBSD Vulkan adapterへ追加せずXCB connection /
  Xlib displayを取得できる
- 現行のVulkan renderer / presenter / swapchain設計を変更する必要がない

### Phase 1のQt version方針

初回実装では、既存frontendの分岐と揃えて

```text
Qt 6.5以上:
    BSD Vulkan X11 WSI正式対応

Qt 6.2～6.4:
    QX11Application自体は存在するが、
    melonPrimeDS側の既存BSD/X11分岐と異なるため
    CI確認なしで正式対応扱いしない

Qt 6.1以下 / Qt 5:
    BSD Vulkan X11 WSIは非対応
    private QPAを新規必須依存にせず明示的failureへ落とす
```

とする。

Qt 6.2～6.4対応を追加したい場合は、別途BSD CI matrixでcompile確認してから
正式範囲を広げる。

---

## 3.2 Waylandについて

BSD Wayland対応を今回のPhase 1の必須条件にはしない。

理由:

Qt 6のpublic `QNativeInterface::QWaylandApplication`から`wl_display*`は取得可能だが、Vulkan surface生成に必要な個別`wl_surface*`をQWindowから取得するpublic APIが十分安定していない。

`QVulkanInstance::surfaceForWindow()`を使えばQtへWSIを委譲できるが、現在のmelonPrimeDSは

```text
melonPrimeDS側がVkSurfaceKHRを生成
↓
melonPrimeDS側がvkDestroySurfaceKHRする
```

というownership modelになっている。

一方、Qtの`QVulkanInstance::surfaceForWindow()`を使ったsurfaceはQt/QWindow側のlifetime管理と関係するため、現在の`MelonPrimeVulkanSurfaceCommon.cpp`へ雑に混ぜると、

- double destroy
- renderer切替時のstale surface
- QWindow lifetimeとの不一致
- adopted VkInstance lifetimeとの不一致

を起こす可能性がある。

したがってPhase 1では、

```text
BSD + X11 -> Vulkan有効
BSD + Wayland -> 明確な理由をログに出してVulkan presenterを使用不可
```

でよい。

将来的にWaylandも正式対応する場合は、surface ownership modelを別Phaseで設計する。

---

# 4. 新規ファイル

以下を追加する。

```text
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceBSD.cpp
```

このファイルの責務はBSD X11上での`VkSurfaceKHR`生成だけとする。

Vulkan renderer、swapchain、GPU rasterizer、present pacingは変更しない。

---

# 5. platform guard

`MelonPrimeVulkanSurfaceBSD.cpp`は明示的にBSDだけへ限定する。

推奨:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && \
    (defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__))
```

「WindowsでもAppleでもLinuxでもない」という否定条件だけでBSDを表現しない。

理由:

将来別のUnix系OSを追加した場合に、意図せずBSD実装へ入ることを防ぐため。

---

# 6. Qt private API禁止

**新規`MelonPrimeVulkanSurfaceBSD.cpp`では**以下を使用禁止とする。

```cpp
#include <qpa/qplatformnativeinterface.h>
```

これは「repository全体からQt private APIを削除する」という意味ではない。
最新branchのshared `Screen.cpp`はQt version/platformによって既存private QPA経路を
持つため、今回のBSD Vulkan実装でその依存範囲を増やさないことが目的。

BSD Vulkan adapterでは以下のpublic Qt APIを使用する。

```cpp
#include <QGuiApplication>
#include <QWindow>
#include <QWidget>
```

正式対応範囲のQt 6.5以上では:

```cpp
auto* x11 =
    qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
```

を使用する。

取得:

```text
connection() -> XCB connection
display()    -> Xlib Display
```

Qt公式では`QX11Application`はQt 6.2から存在するが、Phase 1は前節のとおり
Qt 6.5以上を正式検証範囲とする。

Vulkan側では現行Linux実装と同様にXCB/Xlib型をopaque pointerとして扱ってよい。

Qt 6.1以下 / Qt 5ではprivate QPAへfallbackしてBSD Vulkanを無理に有効化せず、
BSD adapterから具体的failureを返す。

---

# 7. BSD X11 surface生成

## 7.1 Create()の基本処理

`MelonPrimeVulkanSurfaceBSD.cpp`の`Create()`は以下の順序にする。

```text
1. instance / getInstanceProcAddr / widgetのnullチェック
2. widget->winId()でnative windowをrealize
3. QGuiApplication::platformName()取得
4. platform pluginがxcb系であることを確認
5. QNativeInterface::QX11Application取得
6. VK_KHR_xcb_surfaceを試す
7. 失敗した場合VK_KHR_xlib_surfaceへfallback
8. 成功したbackend名をSurface.Backendへ記録
9. failure時は具体的理由をSurface.Failureへ記録
```

---

## 7.2 XCBを第一候補にする

現行Linux実装と同様に、

```cpp
vkCreateXcbSurfaceKHR
```

を最初に試す。

Vulkan functionは直接linkしない。

```cpp
getInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR")
```

から取得する。

XCB connectionはQt public native interfaceから取得する。

概念:

```cpp
auto* x11 =
    qGuiApp->nativeInterface<QNativeInterface::QX11Application>();

if (!x11)
{
    // concrete failure
}

void* connection =
    reinterpret_cast<void*>(x11->connection());
```

window IDは、

```cpp
const auto windowId =
    static_cast<unsigned long long>(widget->winId());
```

を使用する。

既存Linux実装と同じVulkan ABI layoutを使って、

```text
VkXcbSurfaceCreateInfoKHR相当
```

をローカルstructとして定義してよい。

既存方針どおり、

```cpp
VK_USE_PLATFORM_XCB_KHR
```

をshared headerへ拡散しない。

---

## 7.3 Xlib fallback

XCB pathが、

- entry point不存在
- connection取得失敗
- vkCreateXcbSurfaceKHR失敗

のいずれかならXlibへfallbackする。

取得:

```cpp
void* display =
    reinterpret_cast<void*>(x11->display());
```

entry point:

```cpp
vkCreateXlibSurfaceKHR
```

Xlibでも失敗した場合だけsurface生成失敗とする。

---

# 8. platformName判定

Qt pluginの名前を固定文字列一個だけで判断しすぎないこと。

最低限、

```text
xcb
```

を正式対応する。

もしBSD packageでQt plugin名に差異が存在することがCI等で確認できた場合のみ追加する。

未知pluginに対して「たぶんX11」と推測して進めない。

失敗時:

```text
[Vulkan] BSD WSI unavailable: Qt platform plugin '<name>' is not supported
```

のようにplatform plugin名をログへ含める。

---

# 9. Stubの責務変更

現在の

```text
MelonPrimeVulkanSurfaceStub.cpp
```

は最新HEADでもBSDを含む「Windows/macOS/Linux以外」を担当している。

Phase 1でBSD adapterを追加した後は、**Qt 6.5以上のBSD buildでは**
Stubをtargetへ追加しない。

基本方針:

```text
Qt 6.5+ BSD:
    MelonPrimeVulkanSurfaceBSD.cpp

Qt 6.1以下 / Qt 5 BSD:
    BSD adapter自身が明示的unsupported failureを返す
    またはCMakeでStubを選択

unknown Unix:
    MelonPrimeVulkanSurfaceStub.cpp
```

実装を単純にするための推奨は、

```text
BSDでは常にMelonPrimeVulkanSurfaceBSD.cppを選択
    ↓
ファイル内部でQT_VERSIONを判定
    ↓
Qt 6.5+:
    XCB/Xlib WSI

older Qt:
    concrete unsupported failure
```

である。

この方式ならgeneric StubからBSDを明示的に除外できる。

例:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && \
    !defined(_WIN32) && !defined(__APPLE__) && !defined(__linux__) && \
    !defined(__FreeBSD__) && !defined(__NetBSD__) && !defined(__OpenBSD__)
```

**CMake source selectionを主防御、source内guardを第二防御**とする。

---

# 10. CMake変更

対象:

```text
src/frontend/qt_sdl/CMakeLists.txt
```

最新HEADのsource selectionは現在:

```text
Windows -> Win32 adapter
macOS   -> Metal adapter
Linux   -> Linux adapter
else    -> Stub
```

であり、BSDは依然Stubへ入る。

Vulkan frontend source選択を以下へ変更する。

概念:

```cmake
if (WIN32)
    target_sources(melonDS PRIVATE
        MelonPrimeVulkanSurfaceWin32.cpp)
elseif (APPLE)
    target_sources(melonDS PRIVATE
        MelonPrimeVulkanSurfaceMacOS.mm)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(melonDS PRIVATE
        MelonPrimeVulkanSurfaceLinux.cpp)
elseif (
    CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
    OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD"
    OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    target_sources(melonDS PRIVATE
        MelonPrimeVulkanSurfaceBSD.cpp)
else()
    target_sources(melonDS PRIVATE
        MelonPrimeVulkanSurfaceStub.cpp)
endif()
```

BSDを`else()`へ依存させず明示する。

`MelonPrimeVulkanSurfaceBSD.cpp`内部で:

```cpp
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // public QNativeInterface X11 WSI
#else
    // explicit unsupported failure
#endif
```

とし、private QPAを要求しない。

また最新CMakeでは`Qt6GuiPrivate`をnon-Windows/non-Appleで`QUIET`検索する既存処理があるが、
**BSD Vulkan adapterを成立させるために`Qt6GuiPrivate`をREQUIREDへ変更してはいけない。**

---

# 11. Linux実装を今回リファクタしない

今回のBSD対応と同時に、

```text
MelonPrimeVulkanSurfaceLinux.cpp
```

を大規模に書き換えない。

特に、

```text
LinuxのQPA実装を全部QNativeInterfaceへ変更する
Linux/BSDを一つの巨大UnixSurface.cppへ統合する
```

は今回の必須作業に含めない。

理由:

- Linux Vulkanは既にruntime検証経路がある
- BSD対応とLinux WSI refactorを同時に行うと、回帰時に原因を分離しにくい
- 依頼者がBSD実機を持たないため、変更範囲を狭くする価値が高い

BSD対応がCIで安定した後、別commitで共通化を検討する。

---

# 12. VulkanContext監査

対象:

```text
src/VulkanContext.cpp
```

現在Windows/macOS以外では、

```text
VK_KHR_xlib_surface
VK_KHR_xcb_surface
VK_KHR_wayland_surface
```

をruntimeから列挙する。

これはBSD X11対応にも利用できるため、基本設計を変更しない。

ただし変数名やコメントに、

```text
Linux
anyLinuxSurface
```

など、実際にはLinux以外にも使用される表現があれば、

```text
Unix-like WSI
anyUnixSurface
```

などへ修正する。

**挙動変更ではなく意味の修正だけにすること。**

---

# 13. VulkanLoader監査

対象:

```text
src/VulkanLoader.cpp
```

現在Unix系は主に、

```text
libvulkan.so.1
libvulkan.so
```

を`dlopen()`候補としている。

FreeBSD / OpenBSD / NetBSDのGitHub Actions VMで、Vulkan loader package導入後にこの候補で実際にロード可能かをCIで検証する。

重要:

**BSDごとのsonameを推測だけでコードへ大量追加しない。**

CI上で、

```text
libvulkan.so.1 failed
libvulkan.so failed
actual installed soname = ...
```

が確認できたOSだけ、根拠付きでcandidateを追加する。

追加する場合も既存Linux挙動を変えない。

---

# 14. GitHub Actions BSD workflow変更

対象:

```text
.github/workflows/build-bsd.yml
```

現在のmatrix:

```text
FreeBSD
NetBSD
OpenBSD
```

を維持する。

各OSへ以下を追加する。

```text
Vulkan headers
Vulkan loader
利用可能ならMesa Vulkan software/runtime driver
```

パッケージ名は各OSのpackage managerで実在確認して設定する。

単にLinuxのpackage名をコピーしない。

## 14.1 最新workflowの`continue-on-error`をgatingとして扱わない

最新HEADでは`build` jobに:

```yaml
continue-on-error: true
```

が設定されている。

さらにartifact集約job/stepも部分失敗を許容する構成がある。

したがって、

```text
workflow全体がgreen
artifactが1個以上生成
```

だけでは、

```text
FreeBSD PASS
NetBSD PASS
OpenBSD PASS
```

の証明にならない。

今回のBSD Vulkan validationでは以下のどちらかを必須とする。

### 推奨A: strict validation jobを追加

既存artifact packaging jobのtolerant behaviorを壊さず、

```text
bsd-vulkan-validation
```

のような別matrix jobを作成し、

```yaml
continue-on-error: false
```

または指定なしにする。

このjobで3 BSDすべての:

```text
dependency install
Vulkan ON configure
Vulkan backend enabled確認
Vulkan tests
full build
Vulkan OFF configure/build
```

をgatingする。

### B: 既存build jobをstrict化

既存jobをそのままDoD gateとして使うならjob-level
`continue-on-error: true`を削除する。

ただしnightly artifact収集の既存運用へ影響し得るため、初回実装ではAを推奨する。

`workflow_dispatch`は既に存在するため、`develop_remakeVulkan_ver3`の実装commitを
手動実行で検証可能。push triggerが`main`/`ci/*`中心である点に注意し、
「pushしたから自動でBSD 3種が検証された」と誤認しない。

---

# 15. BSD buildでVulkanを明示ONにする

Configure stepへ少なくとも以下を追加する。

```cmake
-DMELONPRIME_ENABLE_VULKAN=ON
-DMELONPRIME_FORCE_DISABLE_VULKAN=OFF
```

現在Vulkan option自体はdefault ONだが、CIの目的を明確化するため明示指定する。

さらにconfigure logで、

```text
MelonPrime Vulkan backend: enabled
```

が出たことを検証する。

「workflowが成功した」だけではVulkan有効buildの証拠にしない。

## 15.1 最新branchで追加済みのVulkan build-time testsも必須化

最新HEADではVulkan active時、通常buildの`ALL` targetに:

```text
melonprime_vulkan_present_timing_check
melonprime_vulkan_present_pacer_dispatch_check
```

が含まれる。

したがってBSD Vulkan ON buildではfull build PASSだけでなく、
log上で少なくとも以下を確認する。

```text
Vulkan present timing model tests:
    PASS

Vulkan present pacer fake-dispatch tests:
    PASS
```

これらはdriver/GPU/window system不要のため、
BSD実機が無い今回のCI validationと相性がよい。

developer-featuresをONにする専用validation variantを追加する場合はさらに:

```text
melonprime_vulkan_renderer_fallback_check
```

もPASSさせる。

ただし通常BSD release buildのためだけにdeveloper-featuresを強制ONへ変更する必要はない。

---

# 16. Vulkan完全OFF buildも残す

既存Windows/Linux workflowと同じ考え方で、BSDでも可能なら別build directoryを作り、

```cmake
-DMELONPRIME_ENABLE_VULKAN=OFF
-DMELONPRIME_FORCE_DISABLE_VULKAN=ON
```

でcompileする。

目的:

- Vulkan sourceがhard build gate内にあること
- Vulkanを無効化してもSoftware/OpenGL buildが壊れないこと
- BSD対応コードがshared frontendへ漏れていないこと

---

# 17. CIで必須にする静的検証

物理BSD実機がないため、CI検証を通常より強くする。

最低限以下を必須にする。

## 17.1 FreeBSD

```text
strict validation job PASS
CMake configure PASS
Vulkan backend enabled確認
MelonPrimeVulkanSurfaceBSD.cpp compile PASS
Vulkan present timing model test PASS
Vulkan present pacer fake-dispatch test PASS
full build PASS
Vulkan OFF build PASS
```

## 17.2 NetBSD

FreeBSDと同じstrict validation項目をすべてPASSさせる。

## 17.3 OpenBSD

FreeBSDと同じstrict validation項目をすべてPASSさせる。

---

# 18. ABI layout test

BSD surface adapter内でVulkan platform create-infoをローカルstructとして定義する場合、

```text
XcbSurfaceCreateInfo
XlibSurfaceCreateInfo
```

についてstatic assertionまたはcompile-time testを追加する。

最低限:

- `sType`
- `pNext`
- `flags`
- connection/display pointer
- window field

の順序がVulkan ABIと一致すること。

既存Linux実装と重複する場合は、値・field orderが完全一致していることを監査する。

---

# 19. Shader検証

BSD対応だからといってSPIR-V生成物を変更しない。

既存Vulkan shader sync checkをBSD対応commitでも通す。

確認:

```text
tools/vulkan/compile-shaders.py --check-source-sync
```

またはrepository既存のVulkan shader audit。

BSD WSI対応とshader logicを同じcommitで変更しない。

---

# 20. runtime smoke test

依頼者にBSD実機が無いため、物理GPU runtime testは要求しない。

ただしGitHub Actions BSD VM内でsoftware Vulkanが利用できる場合は、best-effortでruntime smokeを追加する。

理想:

```text
virtual X11 server
+
software Vulkan driver
+
melonPrimeDS
```

で、

```text
Vulkan loader open
VkInstance create
physical device enumeration
VkSurfaceKHR create
present support query
swapchain create
presenter ready
```

まで確認する。

ROMは不要。

既存Linux Vulkan smoke testと同じ考え方を使う。

---

# 21. runtime smokeを必須化する条件

software Vulkan runtimeが3 BSDすべてで安定して導入可能であることをCI上で確認できた場合のみgating testにする。

package mirrorやVM limitationで不安定な場合は、

```text
build/static validation = gating
runtime smoke = continue-on-error / diagnostic
```

としてよい。

不安定な外部mirrorのためにBSD全体のrelease buildを止めない。

---

# 22. 実機未検証を隠さない

実装完了時の報告には必ず以下を明記する。

```text
FreeBSD physical GPU runtime: NOT TESTED
NetBSD physical GPU runtime: NOT TESTED
OpenBSD physical GPU runtime: NOT TESTED
```

CI上のsoftware Vulkan smokeが成功しても、

```text
BSD Vulkan実機対応確認済み
```

とは表現しない。

正しい表現:

```text
BSD 3種でVulkan-enabled buildを確認。
GitHub Actions VM上のcompile/static validationを通過。
実BSD + physical GPU runtimeは未検証。
```

---

# 23. fallback設計

BSD上でVulkan surface生成に失敗した場合、クラッシュさせない。

最新branchではVulkan renderer fallbackのruntime-failure latchと
panel lifetime保護が強化されているため、BSD対応でも**既存fallback authorityを再利用**する。

```text
Vulkan renderer requested
↓
BSD WSI unavailable / surface create failure
↓
具体的failure log
↓
既存renderer/presentation fallback path
↓
Qt presentation fallback
```

を維持する。

BSD専用の第二fallback state machineを新設しない。

保存済みrenderer設定を勝手にSoftwareへ書き換えない。

fallback時のOSD/panel切替を独自に直接操作せず、
最新branchの`rendererRuntimeFallback` / panel transition contractを壊さない。

---

# 24. failure log要件

最低限以下をログへ出す。

```text
OS
Qt platform plugin
Vulkan loader candidate
enabled WSI extension
selected surface backend
native X11 interface availability
window ID
VkResult
fallback reason
```

成功例:

```text
[Vulkan] BSD presentation surface created backend=VK_KHR_xcb_surface platform=xcb window=...
```

XCB失敗後Xlib成功:

```text
[Vulkan] BSD vkCreateXcbSurfaceKHR failed: ...; trying VK_KHR_xlib_surface
[Vulkan] BSD presentation surface created backend=VK_KHR_xlib_surface ...
```

未対応Wayland:

```text
[Vulkan] BSD Vulkan WSI: Qt platform plugin 'wayland' is not supported by the BSD X11 adapter
```

---

# 25. renderer本体へBSD分岐を散らさない

以下へBSD専用分岐を追加しない。

```text
GPU_Vulkan.cpp
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.cpp
VulkanDevice.cpp
VulkanMemory.cpp
VulkanDescriptors.cpp
VulkanSync.cpp
VulkanPresentPacer.cpp
```

BSD差分は原則として、

```text
MelonPrimeVulkanSurfaceBSD.cpp
CMakeLists.txt
build-bsd.yml
必要な場合のみVulkanLoader.cpp
コメント整理のみVulkanContext.cpp
```

へ限定する。

---

# 26. 低遅延機能

BSD Vulkanを有効化しても、

```text
NVIDIA Reflex
AMD Anti-Lag
```

を「BSDで動作確認済み」と扱わない。

extension probeは既存共通実装に任せる。

runtimeがextensionを提供しなければUI/runtime capability判定で無効になる既存設計を維持する。

BSD対応のためにvendor extensionを強制ONしない。

---

# 27. Config / UI

Vulkan buildがBSDで有効なら、既存Vulkan renderer項目を利用可能にする。

ただしfeature probeが失敗した場合は既存のruntime unavailable処理を使う。

BSD専用renderer IDを新設しない。

```text
VulkanはVulkan
```

として既存renderer backendを共有する。

---

# 28. 検証matrix

## Compile / Build

| OS | Vulkan ON build | Vulkan OFF build | BSD surface TU |
|---|---:|---:|---:|
| FreeBSD x86_64 | 必須 | 必須 | 必須 |
| NetBSD x86_64 | 必須 | 必須 | 必須 |
| OpenBSD x86_64 | 必須 | 必須 | 必須 |

## Runtime

| OS | CI software Vulkan | physical GPU |
|---|---:|---:|
| FreeBSD | 可能なら実施 | NOT TESTED |
| NetBSD | 可能なら実施 | NOT TESTED |
| OpenBSD | 可能なら実施 | NOT TESTED |

## Regression

以下既存workflowを壊さない。

```text
Windows Vulkan
Windows DX12
Linux Vulkan
macOS Vulkan/MoltenVK
macOS Metal
Software
OpenGL
OpenGL Compute
```

---

# 29. 既存OS回帰テスト

BSD対応commit後に最低限以下のGitHub Actionsを確認する。

```text
build-windows.yml
build-ubuntu.yml
build-macos.yml
build-bsd.yml
```

特にLinux Vulkan WSIを変更していないことを確認する。

Windows / macOS source selectionもCMake変更によって誤ってStubへ落ちていないこと。

最新branchではVulkan present-pacer/fallback regression targetが増えているため、
既存OSでも可能な範囲で以下を維持する。

```text
Vulkan present timing model
Vulkan present pacer fake-dispatch
renderer fallback regression（developer buildで対象の場合）
```

BSD対応のためにこれらをdisableしてbuildだけ通すことは禁止する。

---

# 30. 禁止事項

今回の実装では以下を禁止する。

- `defined(__linux__)`を単純削除してLinux QPAコードをBSDへ流用
- BSD Vulkan adapterへprivate Qt QPA headerを必須化
- Qt 6.1以下 / Qt 5でprivate QPAへ迂回してBSD Vulkanを無理に有効化
- Vulkan runtimeが無い場合に無言でSoftwareへfallback
- FreeBSDだけ成功してBSD全体対応済みと表現
- CI compile成功だけでphysical GPU runtime成功と表現
- OpenBSD/NetBSDのlibrary sonameを推測だけで追加
- BSD対応のついでにVulkan renderer本体を大規模refactor
- Linux Vulkan WSIを同時に全面書き換え
- `MELONPRIME_ENABLE_VULKAN` hard gateを破壊
- Vulkan OFF buildへBSD Vulkan symbolを残す
- ROMをCIへ持ち込む
- 実機テストできないことを理由に検証を省略
- `continue-on-error: true`のBSD jobを3 OS PASSのgating evidenceとして扱う
- 最新branchのVulkan present timing / fake-dispatch testをBSDだけ無効化してbuildを通す

---

# 31. 推奨実装順序

## Phase A: Build preparation

1. `build-bsd.yml`へVulkan header/runtime packageを追加
2. BSD Vulkan用strict validation jobを追加、または既存jobをstrict化
3. `MELONPRIME_ENABLE_VULKAN=ON`を明示
4. 現状のままconfigureし、BSDがStub buildになることを確認
5. Vulkan loader/header discovery結果をログ保存
6. 使用Qt versionを各BSD jobでログし、Phase 1正式範囲（Qt 6.5+）を満たすことを確認

## Phase B: BSD X11 adapter

1. `MelonPrimeVulkanSurfaceBSD.cpp`追加
2. public `QNativeInterface::QX11Application`使用
3. XCB surface生成
4. Xlib fallback
5. concrete failure logs追加

## Phase C: CMake selection

1. FreeBSD/NetBSD/OpenBSDを明示分岐
2. BSDではBSD adapterだけcompile
3. unknown UnixはStub維持
4. Vulkan OFF build確認

## Phase D: Static/CI hardening

1. 3 BSD strict Vulkan ON full build
2. 3 BSDでVulkan present timing model test PASS
3. 3 BSDでVulkan present pacer fake-dispatch test PASS
4. Vulkan OFF build
5. ABI/layout audit
6. loader candidate audit
7. shader sync audit
8. git diff --check相当
9. `continue-on-error`で失敗が隠れていないことを確認

## Phase E: Optional runtime smoke

1. software Vulkan driver導入可能性確認
2. virtual X11起動
3. no-ROM Vulkan startup
4. instance/surface/presenter readyまでログ検証
5. 不安定ならdiagnostic扱い

---

# 32. Wayland将来Phase

BSD X11対応完了後、Waylandを別Phaseとして検討する。

候補:

```text
A. Qt private QPAをoptional dependencyとして限定利用
B. QVulkanInstanceへWSI creationを委譲
C. Qt platform plugin側から安全なwl_surface取得手段を追加
```

ただしBを採用する場合は先に以下を解決する。

```text
VkSurfaceKHR ownership
QVulkanInstance lifetime
QWindow::setVulkanInstance lifetime
renderer live switch
swapchain destroy order
surface destroy order
VulkanContext adopted instance lifetime
```

現在の`MelonPrimeVulkanSurfaceCommon.cpp`はmelonPrimeDS側が`vkDestroySurfaceKHR`する前提なので、Qt-owned surfaceをそのまま混ぜない。

---

# 33. Definition of Done

物理BSD実機を持っていない前提で、今回の完了条件は以下とする。

## Source

- [x] `MelonPrimeVulkanSurfaceBSD.cpp`が追加されている
- [x] FreeBSD / NetBSD / OpenBSDだけにbuild gateされている
- [x] BSD Vulkan adapterがQt private QPA headerへ依存していない
- [x] Qt 6.5+でpublic `QNativeInterface::QX11Application`を使用している
- [x] older Qtはprivate QPAへ逃げず、明確なunsupported failureへ落ちる
- [x] XCB WSIが実装されている
- [x] Xlib fallbackが実装されている
- [x] unknown Qt platform pluginは明確にfailする
- [x] Linux WSI実装を壊していない（共通Unix extension判定の意味上のrenameのみ）
- [x] Windows/macOS adapterを変更していない、または必要最小限

## CMake

- [x] BSD 3種がBSD adapterへ入る
- [x] unknown UnixはStubへ入る
- [x] Vulkan header無しでは既存どおりcleanly disabled
- [x] Vulkan hard disableでBSD Vulkan sourceが完全除外される

## CI

- [x] BSD Vulkan validationが`continue-on-error`ではないstrict gateになっている
- [x] FreeBSD Vulkan ON build PASS
- [x] NetBSD Vulkan ON build PASS
- [x] OpenBSD Vulkan ON build PASS
- [x] FreeBSD Vulkan OFF build PASS
- [x] NetBSD Vulkan OFF build PASS
- [x] OpenBSD Vulkan OFF build PASS
- [x] Vulkan backend enabledログを確認
- [x] FreeBSD Vulkan present timing model test PASS
- [x] NetBSD Vulkan present timing model test PASS
- [x] OpenBSD Vulkan present timing model test PASS
- [x] FreeBSD Vulkan present pacer fake-dispatch test PASS
- [x] NetBSD Vulkan present pacer fake-dispatch test PASS
- [x] OpenBSD Vulkan present pacer fake-dispatch test PASS
- [x] shader/generated source audit PASS
- [x] 既存Windows/Linux/macOS workflowsに回帰なし（Ubuntu `31816875432`、Windows `31816881225`、macOS `31816878478` が現行HEADでPASS）

## Runtime

- [ ] CI software Vulkan smokeは可能なら実施（NOT RUN; pure model/fake-dispatch testsはPASS）
- [x] physical FreeBSD GPU runtimeは未検証と明記
- [x] physical NetBSD GPU runtimeは未検証と明記
- [x] physical OpenBSD GPU runtimeは未検証と明記
- [x] runtime smoke未実施でもcompile/static evidenceを保存

---

# 34. 完了報告フォーマット

実装後は以下の形式で報告する。

```markdown
# BSD Vulkan implementation result

## Implemented

- FreeBSD Vulkan X11 WSI
- NetBSD Vulkan X11 WSI
- OpenBSD Vulkan X11 WSI
- VK_KHR_xcb_surface
- VK_KHR_xlib_surface fallback

## Build verification

| OS | Vulkan ON | Vulkan OFF |
|---|---|---|
| FreeBSD | PASS/FAIL | PASS/FAIL |
| NetBSD | PASS/FAIL | PASS/FAIL |
| OpenBSD | PASS/FAIL | PASS/FAIL |

## Runtime verification

| OS | CI software Vulkan | physical GPU |
|---|---|---|
| FreeBSD | PASS/NOT RUN | NOT TESTED |
| NetBSD | PASS/NOT RUN | NOT TESTED |
| OpenBSD | PASS/NOT RUN | NOT TESTED |

## Remaining limitations

- BSD native Wayland Vulkan WSI
- physical GPU validation
- vendor low-latency extension validation

## Regression

- Windows Vulkan:
- Windows DX12:
- Linux Vulkan:
- macOS Vulkan:
- macOS Metal:
```

---

# 35. 参考対象コード

調査時に確認した主要ファイル:

```text
.github/workflows/build-bsd.yml
src/frontend/qt_sdl/CMakeLists.txt
src/frontend/qt_sdl/MelonPrimeVulkanSurface.h
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceCommon.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceLinux.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceBSD.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceStub.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.cpp
src/frontend/qt_sdl/Screen.cpp
src/VulkanContext.cpp
src/VulkanLoader.cpp
tools/testing/vulkan-present-timing-tests.cpp
tools/testing/vulkan-present-pacer-dispatch-tests.cpp
```

---

# 36. 参考資料

Qt公式:

```text
QVulkanInstance
https://doc.qt.io/qt-6/qvulkaninstance.html

QNativeInterface::QX11Application
https://doc.qt.io/qt-6/qnativeinterface-qx11application.html

QNativeInterface
https://doc.qt.io/qt-6/qnativeinterface.html
```

melonPrimeDS:

```text
https://github.com/ag-advania/melonPrimeDS/tree/develop_remakeVulkan_ver3
```

---

# 37. 最重要方針

```text
BSD Vulkan対応はrenderer移植ではなくWSI追加として扱う。

Linuxで動いているVulkan WSIをBSD対応のために壊さない。

BSD Vulkan adapterではQt private QPAへ依存せず、
public QNativeInterface::QX11ApplicationからX11 handleを取得する。
初回の正式検証範囲は最新frontendと揃えてQt 6.5以上とする。

まずXCB、失敗時Xlib。

Waylandはsurface ownershipを整理できるまで別Phase。

BSD実機が無い以上、
GitHub Actions上の3 BSD buildを最大限強くする。

既存`continue-on-error`のtolerant jobをgating成功と誤認せず、
BSD Vulkanについては3 OSすべてが失敗を伝播するstrict validationを用意する。

最新branchのVulkan present timing / production fake-dispatch testsもBSDで通す。

CI成功をphysical GPU成功と偽らない。
```

---

# 38. 実装・検証結果（2026-08-15）

## Implemented

- `src/frontend/qt_sdl/MelonPrimeVulkanSurfaceBSD.cpp` を追加。
- FreeBSD / NetBSD / OpenBSDだけをBSD adapterへ分岐し、unknown UnixはStubへ残した。
- BSD adapterはQt private QPAを使わず、Qt 6.5+のpublic `QNativeInterface::QX11Application`からXCB connection / Xlib displayを取得する。
- `VK_KHR_xcb_surface`を先に試し、失敗時に`VK_KHR_xlib_surface`へフォールバックする。
- X11 create-infoのローカルABI layoutにstandard-layout、field order、サイズのstatic assertionを追加した。
- `src/VulkanContext.cpp`の`anyLinuxSurface`を`anyUnixSurface`へ意味上の名称変更。Linux以外のUnixで必要なinstance extension選択を表現し、renderer coreの挙動は変更していない。
- `.github/workflows/build-bsd.yml`をstrict build jobへ変更し、3 BSDのVulkan headers / loader、Vulkan ON、Vulkan OFF、shader source sync、present timing、present pacer fake-dispatchを検証する導線を追加した。

## Local verification

| Check | Result | Evidence |
|---|---|---|
| Windows official build | PASS | `tools/build/windows/build-mingw.bat --jobs 1 --tail 220` returned 0; Vulkan present timing, pacer fake-dispatch, renderer fallback, and Intel XeLL tests passed |
| Shader source sync | PASS | `tools/vulkan/compile-shaders.py --check-source-sync`; 111 committed SPIR-V modules matched manifest hashes |
| BSD workflow YAML parse | PASS | PyYAML parsed `.github/workflows/build-bsd.yml`; build job has no job-level `continue-on-error` |
| BSD strict ON/OFF matrix | PASS | GitHub Actions run [31816873249](https://github.com/ag-advania/melonPrimeDS/actions/runs/31816873249), all FreeBSD / NetBSD / OpenBSD jobs and All BSD artifacts succeeded |
| Existing Linux / macOS / Windows workflows | PASS | Ubuntu [31816875432](https://github.com/ag-advania/melonPrimeDS/actions/runs/31816875432), macOS [31816878478](https://github.com/ag-advania/melonPrimeDS/actions/runs/31816878478), Windows [31816881225](https://github.com/ag-advania/melonPrimeDS/actions/runs/31816881225); all targeted the current branch HEAD |

## BSD build and runtime status

| OS | Vulkan ON | Vulkan OFF | CI software Vulkan smoke | Physical GPU |
|---|---|---|---|---|
| FreeBSD | PASS | PASS | NOT RUN | NOT TESTED |
| NetBSD | PASS | PASS | NOT RUN | NOT TESTED |
| OpenBSD | PASS | PASS | NOT RUN | NOT TESTED |

The first dispatch (`31812835788`) exposed a NetBSD-specific `find -path`
probe incompatibility. The probe was corrected to test the actual candidate
files in the BSD package prefixes directly, without relying on BSD-specific
`find` expression behavior. The corrected dispatch (`31814100515`) passed all
three OS jobs; the final current-HEAD dispatch (`31816873249`) was run against
commit `24157d40f0723a090a55117448973887c650b8dd` and again passed all three OS
jobs plus All BSD artifacts. The Ubuntu, macOS, and Windows regression
workflows also passed against that same current HEAD. These workflows do not
perform a physical GPU or optional software-Vulkan presentation smoke test.
Native BSD Wayland WSI, vendor low-latency extensions, and physical GPU
validation remain outside the verified scope.

## Commit / push boundary

The pre-existing deletion of
`.codex/melonPrimeDS_VulkanRendererFallback_bc849b0_Push後再監査結果_2026-08-14.md`
is preserved as-is. The implementation, probe correction, scatter-audit fix,
and this instruction/report update are committed and pushed on
`develop_remakeVulkan_ver3`; the strict BSD and cross-platform regression
results are recorded above. Native BSD Wayland WSI, vendor low-latency
extension validation, and physical GPU runtime remain unverified.
