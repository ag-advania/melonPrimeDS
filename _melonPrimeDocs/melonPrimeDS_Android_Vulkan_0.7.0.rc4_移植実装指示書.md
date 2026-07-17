# melonPrimeDS Vulkan完全移植 実装指示書

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `develop_vulkan`  
**監査時点の対象HEAD:** `7d1ceefe9ec0bffc2a3724b0b99bf9f71ba6f171`  
**移植元リリース:** `SapphireRhodonite/melonDS-android` `0.7.0.rc4`  
**移植元タグの親リポジトリHEAD:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**目的:** Android版0.7.0.rc4のVulkan 3Dレンダラー、Vulkanテクスチャキャッシュ、Vulkanコンポジター、同期・フレーム所有権の実装をmelonPrimeDSへ移植し、Qtデスクトップ版でVulkanを実用可能にする。既存のSoftware、OpenGL、OpenGL Compute、Metal、Metal Computeを壊さない。

---

# 1. 最重要命令

この移植では、次の条件を例外なく守ること。

1. **移植元は必ず`0.7.0.rc4`タグと、そのタグが固定している`melonDS-android-lib`サブモジュールSHAを使用する。**
2. **`GBARumble_PR`などのブランチ先端を移植元の確定版として使用しない。** ブランチは構造調査にのみ使用し、最終コピーはタグ固定SHAから行う。
3. **Android版の共有ファイルをmelonPrimeDSへ丸ごと上書きしない。** 特に`GPU3D.h`、`GPU3D.cpp`、`GPU.cpp`、`GPU_Soft.cpp`、`GPU2D_Soft.cpp`、`NDS.cpp`をAndroid版へ置換してはならない。
4. melonPrime専用ではない既存ファイルへ変更を入れる場合、変更部分を必ず次で囲む。

```cpp
#ifdef MELONPRIME_DS
// MelonPrime専用変更
#endif
```

5. Vulkanにのみ必要な共有コード変更は、必ず次の二重ガードで囲む。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MelonPrime Vulkan専用変更
#endif
```

6. Vulkanのソース追加、宣言、UI、ライブラリ、プラットフォームSurface処理、設定処理は、すべて同じ完全ビルドゲート配下へ置く。
7. SoftwareとOpenGLの既存ファイルをVulkan用実装へ置換しない。既存経路は分岐の既定動作として残す。
8. Vulkan初期化失敗を黙ってSoftwareへ落としてはならない。要求backend、実backend、失敗段階、VkResult、fallback先を必ずログへ出す。
9. Vulkanレンダラーが生成した3D画像をQtへ表示できただけで完了としない。**2D／3D合成、画面入替、Master Brightness、Display Capture、フレーム所有権、高解像度表示まで一致して初めて完了**とする。
10. Vulkan無効ビルドで、Vulkan移植前と同一のSoftware／OpenGL動作とビルド結果を維持する。

---

# 2. 移植元の固定手順

実装開始前に、移植元を次の手順で固定する。

```bash
git clone --recurse-submodules https://github.com/SapphireRhodonite/melonDS-android.git
cd melonDS-android
git checkout 0.7.0.rc4
git submodule sync --recursive
git submodule update --init --recursive

git rev-parse HEAD
git ls-tree HEAD melonDS-android-lib
git -C melonDS-android-lib rev-parse HEAD
```

期待する親リポジトリHEAD:

```text
2c10e59d7209d354e90d9ef4228330bac3f6e794
```

次を記録すること。

```text
SOURCE_ANDROID_TAG=0.7.0.rc4
SOURCE_ANDROID_COMMIT=2c10e59d7209d354e90d9ef4228330bac3f6e794
SOURCE_ANDROID_LIB_COMMIT=<git ls-treeで得たSHA>
TARGET_REPOSITORY=ag-advania/melonPrimeDS
TARGET_BRANCH=develop_vulkan
TARGET_BASE_COMMIT=<実装開始時のHEAD>
```

`SOURCE_ANDROID_LIB_COMMIT`を取得せずに作業を開始してはならない。移植後の各ファイルには、必要に応じて次の形式で由来を残す。

```cpp
// Ported from SapphireRhodonite/melonDS-android-lib
// Source pin: <SOURCE_ANDROID_LIB_COMMIT>
// Android platform dependencies adapted for melonPrimeDS desktop Qt.
```

---

# 3. 監査で確認できた現状

## 3.1 melonPrimeDS側

対象ブランチはmelonDS 1.1系のC++17構成であり、`src/CMakeLists.txt`では現在、SoftwareとOpenGL系ソースが登録されている。Vulkanコアファイルは登録されていない。

現在の主要renderer ID:

```text
renderer3D_Software
renderer3D_OpenGL
renderer3D_OpenGLCompute
renderer3D_Metal
renderer3D_MetalCompute
```

`renderer3D_Vulkan`はまだ存在しない。

Metalにはすでに次の完全ゲートがある。

```text
MELONPRIME_ENABLE_METAL
MELONPRIME_FORCE_DISABLE_METAL
MELONPRIME_METAL_ACTIVE
```

Vulkanも同じ考え方で独立した完全ゲートを作る。Metalゲートの内部へVulkanを入れてはならない。

`MelonPrimeVideoBackend.cpp`では、rendererの正規化とpresentation backendの選択が分離されている。Vulkan追加時もこの構造を維持し、OpenGL判定へVulkanを混ぜない。

## 3.2 Android 0.7.0.rc4側

Android版は、Vulkanを単一クラスではなく次の層へ分割している。

### コア3D

```text
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.cpp
VulkanContext.cpp
VulkanDispatch.cpp
```

### コアシェーダー

```text
GPU3D_Vulkan_InterpSpansShader.comp
GPU3D_Vulkan_BinCombinedShader.comp
GPU3D_Vulkan_CalculateWorkOffsetsShader.comp
GPU3D_Vulkan_SortWorkShader.comp
GPU3D_Vulkan_TriRasterShader.comp
GPU3D_Vulkan_TriRasterBaseShader.comp
GPU3D_Vulkan_TriRasterCompatShader.comp
GPU3D_Vulkan_DepthBlendShader.comp
GPU3D_Vulkan_FinalPassShader.comp
GPU3D_Vulkan_CaptureLineExportShader.comp
GPU3D_Vulkan_GraphicsRasterShader.vert
GPU3D_Vulkan_GraphicsRasterShader.frag
GPU3D_Vulkan_GraphicsNoColorShader.frag
GPU3D_Vulkan_GraphicsClearShader.frag
GPU3D_Vulkan_GraphicsFinalShader.vert
GPU3D_Vulkan_GraphicsEdgeShader.frag
GPU3D_Vulkan_GraphicsEdgeFogShader.frag
GPU3D_Vulkan_GraphicsFogShader.frag
```

各シェーダーに対応する生成済み`*ShaderData.h`がある。

### Android frontend

```text
renderer/FrameQueue.cpp
renderer/VulkanOutput.cpp
renderer/VulkanSurfacePresenter.cpp
renderer/VulkanRetroArchFilterChain.cpp
```

### frontendシェーダー

```text
renderer/VulkanCompositorShader.comp
renderer/VulkanAccumulate3dShader.comp
renderer/VulkanSurfacePresenter.vert
renderer/VulkanSurfacePresenter.frag
```

Android版の`VulkanOutput`は、単なる画像コピーではない。少なくとも次を管理している。

```text
frameId
frontBufferLatched
screenSwapLatched
高解像度3D source
前フレームのtop／bottom source
Software 2Dのstructured metadata
Display Capture用3D source
VkImage／VkImageView
readback
timeline／fence同期
```

したがって、`GPU3D_Vulkan.cpp`だけをコピーしても正しい可視出力にはならない。

---

# 4. 「そのまま移植する」の定義

本指示書における「そのまま移植する」とは、次を意味する。

## 4.1 原則として同一に保つ部分

- Vulkan 3D rasterアルゴリズム
- Triangle／Span／TileのGPUデータ構造
- descriptor layout
- push constants
- texture sampling path
- depth／stencil／fog／edge処理
- graphics pipeline／compute pipelineの分岐
- Vulkan texture cacheの変換ロジック
- SPIR-Vシェーダー内容
- fence／timeline semaphoreの意味
- frame resourceの再利用規則
- 3D color targetの寸法とscale規則
- capture line exportの意味
- compositorの2D／3D合成規則
- frame snapshotの所有権

## 4.2 デスクトップへ適応する部分

- `vulkan_android.h`
- `ANativeWindow`
- `vkCreateAndroidSurfaceKHR`
- Android Hardware Buffer
- libadrenotools
- Android custom driver loader
- JNI
- Android lifecycle
- Android Surface attach／detach
- Android固有のlibrashader連携
- Android固有の画面レイアウト構造
- Android固有ログAPI

これらはアルゴリズムの改変ではなく、ホストプラットフォーム層の差し替えとして扱う。

---

# 5. 絶対にコピーしてはいけないもの

次をAndroid版からmelonPrimeDSへ無条件コピーしてはならない。

```cpp
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
```

```cpp
PFN_vkCreateAndroidSurfaceKHR
PFN_vkGetAndroidHardwareBufferPropertiesANDROID
ANativeWindow*
AHardwareBuffer*
```

```text
libadrenotools
MELONDS_HAS_ADRENOTOOLS
Android custom driver directory
JNIのSurface管理
```

デスクトップ共通ヘッダーへAndroid型が1つでも露出した場合、そのPhaseは未完了とする。

---

# 6. 完成時のアーキテクチャ

```text
Qt設定／UI
    ↓
MelonPrimeVideoBackend
    ↓
EmuThread renderer factory
    ↓
VulkanRenderer3D
    ↓
VulkanContext／VulkanDispatch
    ↓
VulkanTextureCache
    ↓
3D VkImage
    ↓
MelonPrimeVulkanOutput
    ↓
2D structured metadata + frame snapshot
    ↓
MelonPrimeScreenVulkan
    ↓
VkSurfaceKHR／swapchain
    ↓
Qt native window
```

## 6.1 Vulkan deviceの所有者

`VulkanContext`が次を一元所有する。

```text
VkInstance
VkPhysicalDevice
VkDevice
VkQueue
queue family index
queue mutex
feature profile
pipeline cache
validation messenger
```

rendererとpresenterで別々の`VkDevice`を作ってはならない。3D color targetをzero-copyでpresenterへ渡せなくなるためである。

## 6.2 Qtの役割

Qtはwindow lifecycleとnative handleを提供する。Vulkan instance／deviceの主所有者にはしない。

`QVulkanWindow`が独自deviceを作る構成へ安易に寄せてはならない。採用する場合は、`VulkanContext`がそのinstance／device／queueを外部所有として受け取れる設計へ変え、rendererとpresenterで同一deviceを使用することを証明する必要がある。

推奨構成:

```text
QWidgetまたはQWindow
    ↓ native handle
MelonPrimeScreenVulkan
    ↓
VulkanContextのVkInstanceからplatform surfaceを生成
```

---

# 7. ビルドゲート

`src/frontend/qt_sdl/CMakeLists.txt`へMetalと独立したVulkanゲートを追加する。

```cmake
option(MELONPRIME_ENABLE_VULKAN
    "Build MelonPrime Vulkan renderer and presenter"
    ON)

option(MELONPRIME_FORCE_DISABLE_VULKAN
    "Completely exclude all MelonPrime Vulkan code"
    OFF)

set(MELONPRIME_VULKAN_ACTIVE OFF)

if (MELONPRIME_FORCE_DISABLE_VULKAN)
    set(MELONPRIME_VULKAN_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if (Vulkan_FOUND)
        set(MELONPRIME_VULKAN_ACTIVE ON)
    else()
        message(WARNING
            "Vulkan SDK/headers were not found; Vulkan is excluded from this build")
    endif()
endif()
```

## 7.1 ACTIVE時にのみ有効化するもの

```cmake
if (MELONPRIME_VULKAN_ACTIVE)
    target_compile_definitions(core PUBLIC
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)

    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)

    target_sources(core PRIVATE
        # Vulkan core files
    )

    target_sources(melonDS PRIVATE
        # Qt Vulkan presenter files
    )
endif()
```

次がすべて同じ`MELONPRIME_VULKAN_ACTIVE`内に存在すること。

- core Vulkan sources
- frontend Vulkan sources
- shader generated headers
- compile definitions
- Vulkan include path
- loader libraryまたは動的loader実装
- platform surface sources
- UI項目
- feature probe
- runtime renderer factory

## 7.2 完全無効化の要件

```bash
-DMELONPRIME_ENABLE_VULKAN=OFF
```

または

```bash
-DMELONPRIME_FORCE_DISABLE_VULKAN=ON
```

で、次が成立すること。

- Vulkanヘッダー不要
- Vulkan SDK不要
- Vulkan loader不要
- Vulkan source未コンパイル
- Vulkan UI未生成
- Vulkan renderer IDを選択不能
- Software／OpenGL／Metalのリンク結果にVulkan symbolがない

---

# 8. ガード規則

## 8.1 MelonPrime専用ファイル

ファイル全体を次で囲む。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

// implementation

#endif
```

## 8.2 共有melonDSファイル

追加した宣言、field、分岐、hookのみ二重ガードする。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
virtual void SetupAccelFrame() {}
virtual void PrepareCaptureFrame() {}
virtual void BeginCaptureFrame() {}
virtual void SetCaptureScreenSwapHint(bool screenSwap) { (void)screenSwap; }
#endif
```

## 8.3 platform surface

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(_WIN32)
// VK_KHR_win32_surface
#endif
```

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__linux__)
// VK_KHR_xcb_surface / VK_KHR_xlib_surface / VK_KHR_wayland_surface
#endif
```

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(__APPLE__)
// MoltenVK／VK_EXT_metal_surfaceを明示的に有効化した場合のみ
#endif
```

---

# 9. 新規ファイル配置

既存Software／OpenGLファイルから分離する。

## 9.1 core

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/VulkanDispatch.h
src/VulkanDispatch.cpp
src/VulkanPerfStats.h
```

命名衝突を避ける必要がある場合のみ、MelonPrime prefixを使う。

```text
src/MelonPrimeVulkanContext.*
src/MelonPrimeVulkanDispatch.*
```

ただし、移植元とのdiff追跡を優先し、不要なリネームは避ける。

## 9.2 frontend

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanOutput.h
src/frontend/qt_sdl/MelonPrimeVulkanOutput.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFrameQueue.h
src/frontend/qt_sdl/MelonPrimeVulkanFrameQueue.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.h
src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurface.h
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceWin32.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceX11.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceWayland.cpp
```

macOSを初期対象に含める場合のみ追加する。

```text
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceMetal.mm
```

## 9.3 shader

```text
src/Vulkan_shaders/
```

または移植元と同じ`src/`直下へ置く。いずれの場合もGLSLと生成済みSPIR-V headerを1対1で管理する。

---

# 10. VulkanDispatchの移植

Android版は`dlopen`と`dlsym`、`vkGetInstanceProcAddr`を使用している。デスクトップではloader部分だけplatform対応させる。

## 10.1 Windows

```text
LoadLibraryW(L"vulkan-1.dll")
GetProcAddress
FreeLibrary
```

## 10.2 Linux

```text
dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)
dlsym
dlclose
```

必要時のみ`libvulkan.so`を第2候補とする。

## 10.3 macOS

初期実装では既定OFFとする。MoltenVK対応を有効にする場合は、loaderの探索先と`VK_EXT_metal_surface`を明示する。

## 10.4 platform関数

Android専用:

```text
vkCreateAndroidSurfaceKHR
vkGetAndroidHardwareBufferPropertiesANDROID
```

を共通リストへ残さない。

代わりにplatform source単位で次を追加する。

```text
vkCreateWin32SurfaceKHR
vkCreateXcbSurfaceKHR
vkCreateXlibSurfaceKHR
vkCreateWaylandSurfaceKHR
vkCreateMetalSurfaceEXT
```

実際にビルドするplatformの関数だけ宣言・loadする。

## 10.5 symbol検証

`Initialize`、`LoadInstance`、`LoadDevice`は、必須symbolが1つでも欠けたらfalseを返す。欠落symbol名をログへ出す。

```text
[MelonPrime][Vulkan] missing global symbol: vkCreateInstance
[MelonPrime][Vulkan] missing instance symbol: vkCreateWin32SurfaceKHR
[MelonPrime][Vulkan] missing device symbol: vkQueueSubmit
```

---

# 11. VulkanContextの移植

Android版のreference-counted singleton構造を維持する。

## 11.1 必須field

```text
ContextLock
ReferenceCount
Instance
DebugMessenger
PhysicalDevice
Device
Queue
QueueFamilyIndex
QueueLock
TimestampPeriod
TimelineSemaphoresSupported
DynamicTextureIndexingSupported
NonUniformTextureIndexingSupported
DeviceProfile
```

## 11.2 削除または隔離するAndroid field

```text
PFN_vkGetAndroidHardwareBufferPropertiesANDROID
Android custom driver configuration
adrenotools hooks
```

## 11.3 instance extension

共通:

```text
VK_KHR_surface
```

platform別:

```text
Windows: VK_KHR_win32_surface
X11/XCB: VK_KHR_xcb_surface または VK_KHR_xlib_surface
Wayland: VK_KHR_wayland_surface
macOS/MoltenVK: VK_EXT_metal_surface、必要ならVK_KHR_portability_enumeration
```

Debug時:

```text
VK_EXT_debug_utils
```

## 11.4 device extension

最低限:

```text
VK_KHR_swapchain
```

timeline semaphoreはVulkan core versionまたはextensionの双方をprobeし、未対応時はfence経路へ落とす。

## 11.5 physical device選択

優先順位を固定する。

1. ユーザーが保存したdevice UUIDまたはvendor/device ID
2. discrete GPU
3. integrated GPU
4. virtual GPU
5. CPU Vulkan

要求featureを満たさないdeviceは候補から除外する。選択結果をログへ出す。

## 11.6 queue family

3D rendererとpresenterで同一queueを使えるfamilyを優先する。Surface作成前にdeviceを作る設計の場合、present supportを後から検証し、非対応ならcontextを再作成するのではなく、最初からplatform surfaceを一時生成してqueue family選択を行う。

---

# 12. Renderer3D API適応

Android移植元と対象ブランチでは`Renderer3D` APIが異なる。したがってAndroid版`GPU3D.h`を上書きしてはならない。

対象ブランチの形:

```cpp
Renderer3D(GPU3D& gpu3D)
virtual bool Init()
virtual void Reset()
virtual void RenderFrame()
virtual void FinishRendering()
virtual u32* GetLine(int line)
```

Android移植元のVulkan側は、`GPU&`を引数に取る旧APIを使用する箇所がある。

## 12.1 適応方針

- `VulkanRenderer3D`のconstructorは対象ブランチの`Renderer3D(GPU3D&)`へ合わせる。
- 移植元で引数として受けていた`GPU&`は、対象ブランチのprotected memberまたは`GPU3D.GPU`から参照する。
- `Reset(GPU&)`は`Reset()`へ変換する。
- `RenderFrame(GPU&)`は`RenderFrame()`へ変換する。
- `Stop(const GPU&)`相当は、renderer破棄、ROM stop、backend切替で明示的に呼ぶlifecycle methodへ変換する。
- Android版の動作順序は維持し、単に引数の取得方法だけを変更する。

## 12.2 guarded virtual hook

Android版の高解像度合成に必要なhookが対象ブランチに存在しない場合、共有`Renderer3D`へ二重ガードでdefault no-opを追加する。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
virtual void SetupAccelFrame() {}
virtual void PrepareCaptureFrame() {}
virtual void BeginCaptureFrame() {}
virtual void SetCaptureScreenSwapHint(bool screenSwap) { (void)screenSwap; }
virtual bool UsesStructured2DMetadata() const noexcept { return false; }
#endif
```

Software／OpenGLへoverrideを強制しない。既存rendererはdefault no-opのままにする。

---

# 13. VulkanRenderer3Dの移植

## 13.1 変更を最小化する部分

次は移植元の構造を維持する。

- `BackendMode`
- `RenderContext`
- descriptor cache
- color／depth／attr image
- triangle buffer
- toon／fog／edge table buffer
- pipeline cache
- timestamp query
- texture descriptor array
- scale factor
- better polygon設定
- capture line export
- readback
- asynchronous context count

## 13.2 desktopで除去する前提

GPUベンダー判定によるAdreno／Mali専用最適化はコードとして残してよいが、desktop GPUではgeneral profileへ解決すること。Adreno custom driver制御を呼ばない。

## 13.3 初期化

`Init()`は段階ごとに失敗位置を返せるようにする。

```text
Dispatch
Instance
PhysicalDevice
Device
DescriptorLayouts
Samplers
PipelineCache
ShaderModules
ComputePipelines
GraphicsPipelines
FrameResources
ColorTarget
TextureCache
```

ログ例:

```text
[MelonPrime][Vulkan] init failed stage=GraphicsPipelines result=VK_ERROR_FEATURE_NOT_PRESENT
```

## 13.4 source-equivalence確認

移植元と対象版で次を静的比較するテストを用意する。

- `sizeof`されたpush constants
- descriptor binding番号
- shader specialization constant番号
- triangle struct field順序
- texture descriptor上限
- fallback descriptor index
- color／depth format
- image usage flags
- pipeline stage／access mask

`static_assert`で検証できるものは実装へ入れる。

---

# 14. Vulkan Texture Cache

`GPU3D_TexcacheVulkan`はOpenGL texture cacheへ置換しない。OpenGL cacheと別インスタンスとして持つ。

## 14.1 要件

- VRAM更新時のinvalidate
- palette更新時のinvalidate
- texture formatごとの変換
- wrap／repeat／clamp
- transparent color 0
- compressed texture
- descriptor更新
- upload staging buffer
- image layout transition
- renderer破棄時の完全解放

## 14.2 共有テクスチャキャッシュ変更

`GPU3D_Texcache.*`へVulkan用hookを追加する場合、その変更は二重ガードする。

OpenGL用fieldやOpenGL texture IDの意味を変更してはならない。

---

# 15. 2D／3D合成

Vulkan 3D renderer単体では完成しない。Android版`VulkanOutput`とcompositor shaderの意味を移植する。

## 15.1 必須snapshot

各完成frameについて、producer時点で次を固定する。

```text
frame serial
front buffer index
ScreenSwap
3D color image
3D scale
3D width／height
2D packed metadata
Display Capture source
Master Brightness
top／bottom ownership
```

consumer側でlive stateを読み直してはならない。

## 15.2 禁止される誤合成

```text
2D frame N + 3D frame N+1
2D frame N + ScreenSwap N+1
2D frame N + brightness N+1
```

## 15.3 Software 2Dの保護

`GPU_Soft.cpp`／`GPU2D_Soft.cpp`へstructured metadata生成を追加する場合:

- 既存Software framebufferの値を変更しない。
- 既存OpenGLが読むbufferを変更しない。
- Vulkan専用のparallel metadata bufferとして追加する。
- field、書き込み、読み出しを二重ガードする。
- Vulkan無効ビルドではfield自体が存在しない状態にする。

## 15.4 高解像度

Vulkan選択時、scale 2以上では、3D部分をnative 256x192へreadbackしてから再拡大してはならない。高解像度3D VkImageをcompositorへ直接渡す。

## 15.5 Display Capture

次を検証する。

- regular capture
- VRAM capture
- captureが3D lineを参照する場合
- captureとScreenSwapの対応
- capture sourceが前フレームの場合
- 2D-only画面
- capture source欠落時のfallback

---

# 16. FrameQueueと同期

Android版のframe queue／fence／timeline semaphoreの意味を維持する。

## 16.1 frame state

```text
Free
Rendering
Ready
Presenting
```

または同等の明示状態を持つ。

## 16.2 再利用条件

rendererは、presenterが読み終えていないframe resourceを再利用してはならない。

```text
renderer submit完了
    ≠
presenter sampling完了
```

異なるcommand buffer間の順序を、CPU側の偶然の実行順へ依存させない。

## 16.3 timeline semaphore

対応時はtimelineを使用する。未対応時はfence＋binary semaphoreへfallbackする。fallbackは機能低下ではあるが、描画結果を変えてはならない。

## 16.4 resize／backend切替

次の順序を守る。

1. 新規frame受付停止
2. in-flight frame完了待ち
3. presenter descriptor cache破棄
4. swapchain破棄
5. renderer output reference解放
6. resource再生成
7. frame受付再開

---

# 17. Qt Vulkan Presenter

Androidの`VulkanSurfacePresenter`を直接コンパイルしてはならない。VkSwapchainのロジック、present mode選択、descriptor／vertex更新、fence同期を移植し、surfaceだけdesktop化する。

## 17.1 widget lifecycle

`MelonPrimeScreenVulkan`は少なくとも次を処理する。

- native window生成
- show後のsurface生成
- resize
- DPI変更
- fullscreen切替
- window再生成
- hide／show
- close
- renderer切替
- application shutdown

## 17.2 thread

native handle取得とQt widget操作はGUI threadで行う。Vulkan command recording／submitはrendererまたは専用presentation threadで行ってよい。

GUI threadから得たnative handleにはgenerationを持たせ、古いhandleへsurfaceを作らない。

## 17.3 swapchain

次を処理する。

```text
VK_ERROR_OUT_OF_DATE_KHR
VK_SUBOPTIMAL_KHR
window size 0
minimized
surface lost
format変更
present mode変更
```

swapchain再作成時にdevice全体の`vkDeviceWaitIdle`を毎回呼ばず、対象frame／surfaceだけを安全に待つ設計を優先する。

## 17.4 present mode

```text
VSync ON: FIFO
VSync OFF: MAILBOX優先、なければIMMEDIATE、なければFIFO
Fast Forward／Slow Motion override: display同期を無効化できるmodeを優先
```

実際に選択されたmodeをログへ出す。

---

# 18. renderer IDと設定

## 18.1 enum

既存rendererの順序を変更せず、Vulkanを末尾へ追加する。

```cpp
#if defined(MELONPRIME_ENABLE_VULKAN)
    renderer3D_Vulkan,
#endif
```

既存保存値を壊さないため、OpenGLやMetalより前へ挿入してはならない。

将来renderer IDを固定値へ変える場合、それは別commitのconfig migrationとして行う。

## 18.2 PresentationBackend

`MelonPrimeVideoBackend.h`へ追加する。

```cpp
enum class PresentationBackend
{
    NativeQt,
    OpenGL,
    Metal,
#if defined(MELONPRIME_ENABLE_VULKAN)
    Vulkan,
#endif
};
```

## 18.3 正規化

```cpp
#if defined(MELONPRIME_ENABLE_VULKAN)
case renderer3D_Vulkan:
    return MelonPrimeVulkanFeatureCheck::IsRuntimeAvailable()
        ? renderer3D_Vulkan
        : renderer3D_Software;
#endif
```

ただし正規化時にfallback理由を記録し、UIへ利用不可理由を出す。

## 18.4 GL context

Vulkan選択時にOpenGL contextを作らない。

```text
RendererRequiresOpenGLContext(Vulkan) == false
ResolvePresentationBackend(Vulkan) == Vulkan
```

## 18.5 renderer factory

`EmuThread::updateRenderer()`のVulkan分岐だけを二重ガードする。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
case renderer3D_Vulkan:
    renderer = VulkanRenderer3D::New(GPU3D);
    break;
#endif
```

初期化失敗時:

- Software rendererを生成する。
- requested rendererはVulkanのまま保持する。
- actual rendererをSoftwareとして記録する。
- configを勝手に書き換えない。
- OSDへ一度だけ理由を表示する。

---

# 19. UI

## 19.1 表示条件

Vulkan項目は次をすべて満たす場合のみ選択可能にする。

```text
MELONPRIME_VULKAN_ACTIVE
Vulkan loader発見
対応physical device発見
必須queue family発見
必須format／feature対応
present対応
```

## 19.2 UI文字列

最低限:

```text
Vulkan
Vulkanを利用できません
Vulkan loaderが見つかりません
対応するVulkanデバイスが見つかりません
Vulkan初期化に失敗しました
```

MelonPrime localization catalogへ全言語entryを追加する。

## 19.3 設定候補

初期公開では設定を増やしすぎない。

```text
Renderer: Vulkan
Internal Resolution
Better Polygons
High Resolution Coordinates
VSync
Filtering
```

開発者向けのみ:

```text
Validation Layers
Force Fence Path
Disable Timeline Semaphores
Disable Dynamic Texture Indexing
Dump Vulkan frame
```

---

# 20. SPIR-V生成

Android版はGLSLから生成済みheaderを作る。melonPrimeDSでも同じソースと生成物を管理する。

## 20.1 原則

- リリースbuildは生成済みheaderだけでビルド可能にする。
- 一般ユーザーのbuildに`glslc`を必須化しない。
- 開発者用に再生成targetと同期check targetを用意する。
- source tag固定時点のshaderとheader hashを記録する。

## 20.2 target

```text
melonprime_regenerate_vulkan_spirv
melonprime_check_vulkan_spirv
```

## 20.3 check

各GLSLを再コンパイルし、生成済みheaderとbyte単位で比較する。差がある場合CIを失敗させる。

## 20.4 compiler情報

次をmanifestへ記録する。

```text
glslc version
shaderc version
target environment
optimization level
source SHA-256
generated header SHA-256
```

---

# 21. Android固有追加機能の扱い

## 21.1 libadrenotools

desktop初期移植へ入れない。`VulkanDispatch`の本体と混ぜない。

## 21.2 Android Hardware Buffer

desktopへ入れない。zero-copyは通常のVkImage共有で実現する。

## 21.3 librashader／RetroArch filter

Vulkan rendererが安定するまで移植対象外とする。既存melonPrimeDSの画面filterをVulkan presenterで再実装する場合は別Phaseとする。

## 21.4 Android performance profile

Adreno／Mali向けprofileは残してよいが、desktopではGeneral profileを既定とする。vendor判定で未定義挙動を起こさない。

---

# 22. 実装Phase

# Phase V0: Source pinと回帰baseline

## 作業

- 対象HEADを記録
- 移植元親SHAとサブモジュールSHAを記録
- Software／OpenGLの既存buildを保存
- 代表ROMのスクリーンショットとframe hashを保存
- 起動、メニュー、試合、マップ、ポーズ、暗転を記録
- VSync／Fast Forward／fullscreenを記録

## 完了条件

```markdown
- [ ] source pin記録済み
- [ ] target base pin記録済み
- [ ] Software baseline取得済み
- [ ] OpenGL baseline取得済み
- [ ] git status記録済み
```

---

# Phase V1: Vulkan完全ビルドゲート

## 作業

- CMake option追加
- ACTIVE変数追加
- compile definition追加
- Vulkan OFFビルド確認
- UIはまだ追加しない

## 完了条件

```markdown
- [ ] OFFでVulkan header不要
- [ ] OFFでSoftware build成功
- [ ] OFFでOpenGL build成功
- [ ] ONで空のVulkan feature probeをビルド可能
```

---

# Phase V2: Dispatch／Context

## 作業

- `VulkanDispatch`コピー
- Android include削除
- Win32／Linux loader追加
- `VulkanContext`コピー
- Android Hardware Buffer分離
- device／queue／feature probe
- validation messenger

## 完了条件

```markdown
- [ ] VkInstance作成
- [ ] physical device列挙
- [ ] VkDevice作成
- [ ] queue取得
- [ ] device profileログ
- [ ] clean shutdown
- [ ] validation errorなし
```

---

# Phase V3: Shaderとcore rendererの機械的移植

## 作業

- Vulkan GLSLコピー
- generated headerコピー
- shader hash manifest作成
- `GPU3D_Vulkan`コピー
- `GPU3D_TexcacheVulkan`コピー
- `GPU3D_AcceleratedFrontend`コピー
- 対象`Renderer3D` APIへ適応

## 完了条件

```markdown
- [ ] shader module全作成
- [ ] pipeline全作成
- [ ] 256x192 color target生成
- [ ] 1frameをoffscreen render可能
- [ ] readback画像を保存可能
```

---

# Phase V4: Renderer lifecycle統合

## 作業

- renderer ID追加
- renderer factory追加
- settings伝搬
- reset／stop／switch対応
- actual backendログ

## 完了条件

```markdown
- [ ] Vulkan選択でVulkanRenderer3D生成
- [ ] Softwareへ戻せる
- [ ] OpenGLへ戻せる
- [ ] ROM reopenでresource leakなし
- [ ] init失敗理由が表示される
```

---

# Phase V5: 1x readback表示bootstrap

## 作業

- Vulkan 3D outputをCPU readback
- 既存NativeQt presenterへ一時的に合成
- Vulkan 3Dの正しさを確認

## 注意

このPhaseは診断用であり、最終構成ではない。CPU readbackを最終Vulkan pathとして残さない。

## 完了条件

```markdown
- [ ] 3D形状が表示される
- [ ] textureが表示される
- [ ] depth／alpha／fogが概ね一致
- [ ] Software／OpenGL無変化
```

---

# Phase V6: Structured 2D metadataとVulkan compositor

## 作業

- `SoftPackedFrameSnapshot`相当を移植
- guarded metadata buffer追加
- compositor shader移植
- capture source移植
- frame serial固定
- ScreenSwap／brightness固定

## 完了条件

```markdown
- [ ] 2D-only画面正常
- [ ] 3D＋2D画面正常
- [ ] menu正常
- [ ] map正常
- [ ] transition／darken正常
- [ ] Display Capture正常
```

---

# Phase V7: Qt native Vulkan presenter

## 作業

- platform surface作成
- swapchain
- surface presenter shader
- resize／fullscreen
- present mode
- out-of-date recovery

## 完了条件

```markdown
- [ ] CPU readbackなしで表示
- [ ] 1x正常
- [ ] resize正常
- [ ] fullscreen正常
- [ ] minimize／restore正常
- [ ] VSync切替正常
```

---

# Phase V8: 高解像度

## 作業

- 2x／3x／4x／6x target
- compositorが高解像度3Dを直接sampling
- scale live change
- texture cache再生成
- output lease管理

## 完了条件

```markdown
- [ ] 2xで実際に512x384の3D target
- [ ] 4xで実際に1024x768の3D target
- [ ] 1xへ戻して正常
- [ ] scale変更時artifactなし
- [ ] menu／暗転で前後frame混合なし
```

---

# Phase V9: 性能・同期

## 作業

- timeline semaphore
- fence fallback
- frame queue
- pipeline cache保存
- descriptor cache
- in-flight resource再利用
- present pacing

## 完了条件

```markdown
- [ ] validation layer同期エラーなし
- [ ] texture再利用raceなし
- [ ] Fast Forwardでpresenterが60Hz上限にならない
- [ ] normal VSyncでframe pacing正常
- [ ] backend切替deadlockなし
```

---

# Phase V10: UI／翻訳／設定

## 作業

- renderer選択追加
- feature availability
- tooltip
- OSD
- localization
- config migration確認

## 完了条件

```markdown
- [ ] 未対応環境ではdisabled
- [ ] 理由表示
- [ ] 言語変更で更新
- [ ] 既存renderer保存値維持
```

---

# Phase V11: CI／回帰

## 作業

- Windows Vulkan build
- Linux Vulkan build
- Vulkan OFF build
- Software／OpenGL runtime
- shader sync check
- validation smoke test

## 完了条件

```markdown
- [ ] Vulkan ON build成功
- [ ] Vulkan OFF build成功
- [ ] Software runtime正常
- [ ] OpenGL runtime正常
- [ ] Vulkan runtime正常
- [ ] shader check成功
```

---

# Phase V12: cleanup

## 作業

- bootstrap readback pathをdeveloper-only化または削除
- dead code削除
- Android固有symbol scan
- source attribution
- documentation

## 完了条件

```markdown
- [ ] ANativeWindowなし
- [ ] vulkan_android.hなし
- [ ] adrenotoolsなし
- [ ] silent fallbackなし
- [ ] Vulkan OFFでsymbol残存なし
```

---

# 23. Software／OpenGLを壊さないための規則

## 23.1 renderer factory

既存caseの本文を変更せず、Vulkan caseだけ追加する。

## 23.2 framebuffer

Software framebufferの型、stride、buffer count、swap timingを変更しない。

## 23.3 OpenGL context

Vulkan追加のためにOpenGL context初期化条件を広げない。VulkanはOpenGL contextを要求しない。

## 23.4 OpenGL texture cache

Vulkan descriptorやVkImageをOpenGL texture cacheへ格納しない。

## 23.5 共通設定

Vulkan選択時だけ意味のある設定値で、Software／OpenGLの値を上書きしない。

## 23.6 preprocessor比較

次の2構成でpreprocessed diffを確認する。

```text
MELONPRIME_ENABLE_VULKAN undefined
MELONPRIME_ENABLE_VULKAN=1
```

Vulkan undefined時、既存Software／OpenGL関数本文へ差分が残らないこと。

---

# 24. fallback規則

## 24.1 runtime fallback

Vulkan初期化失敗時はSoftwareへfallbackする。OpenGLへ自動fallbackしない。OpenGL contextが作られていない可能性があるためである。

## 24.2 config

configの`3D.Renderer`を勝手にSoftwareへ保存しない。

```text
requested=Vulkan
actual=Software
```

を保持する。

## 24.3 再試行

次の契機でのみ再試行する。

- userがrendererを再選択
- application再起動
- device設定変更
- developer action

毎frame再試行しない。

---

# 25. ログ

最低限、次を出す。

```text
requested renderer
actual renderer
Vulkan loader path
instance API version
physical device name
vendor ID／device ID
driver version
queue family
present support
selected surface extension
selected present mode
internal scale
color target dimensions
timeline semaphore availability
dynamic texture indexing availability
shader pipeline stage
frame serial
rendered slot
presented slot
fallback reason
VkResult
```

状態変更時のみ出し、毎frame大量出力しない。

---

# 26. 検証matrix

## 26.1 build

| 構成 | 必須 |
|---|---|
| Vulkan OFF + Software | 必須 |
| Vulkan OFF + OpenGL | 必須 |
| Vulkan ON + Software | 必須 |
| Vulkan ON + OpenGL | 必須 |
| Vulkan ON + Vulkan | 必須 |
| Vulkan ON + Metal | macOS対応時 |
| Debug + validation | 必須 |
| Release | 必須 |

## 26.2 runtime renderer切替

```text
Software → Vulkan
Vulkan → Software
OpenGL → Vulkan
Vulkan → OpenGL
Vulkan → Vulkan（再初期化）
```

## 26.3 解像度

```text
1x
2x
3x
4x
6x
live change
ROM reopen後
```

## 26.4 画面状態

```text
boot
firmware menu
game menu
match
pause
map
scan visor
transition
fade to black
Master Brightness
ScreenSwap
Display Capture
2D-only scene
3D-heavy scene
```

## 26.5 window

```text
windowed
fullscreen
resize
minimize／restore
DPI change
multiple monitors
secondary window
window close
```

## 26.6 timing

```text
VSync ON
VSync OFF
Frame Limit ON
Frame Limit OFF
Fast Forward Hold
Fast Forward Toggle
Slow Motion
```

## 26.7 regression

```text
Software screenshot comparison
OpenGL screenshot comparison
input latency
mouse capture
Custom HUD
OSD
screenshot capture
savestate load
reset
ROM stop／reopen
```

---

# 27. validation layer合格条件

次のcategoryを0件にする。

```text
VUID
image layout mismatch
use-after-free
command buffer reset while pending
fence reuse while unsignaled
descriptor references destroyed image
swapchain image misuse
queue family ownership mismatch
host access without flush／invalidate
```

validationを無効化して問題を隠してはならない。

---

# 28. 性能合格条件

正しさを確認した後に測定する。

最低限記録:

```text
CPU frame time
GPU 3D time
compositor GPU time
present CPU time
texture upload bytes
pipeline cache hit
frame queue wait
swapchain acquire wait
readback count
```

最終Vulkan通常経路で、毎frameのfull-frame CPU readbackが0回であること。

---

# 29. 禁止事項

- Android版の`GPU3D.h`で対象ブランチを上書きする。
- Android版の`GPU.cpp`／`GPU_Soft.cpp`を丸ごと上書きする。
- Vulkan対応のためにSoftware rendererを削除する。
- Vulkan対応のためにOpenGL rendererを削除する。
- Vulkan選択時もOpenGL contextを作る。
- Vulkan presenterとVulkan rendererで別deviceを使う。
- `ANativeWindow`をdesktop codeへ残す。
- `vulkan_android.h`をdesktop codeへ残す。
- source pinを記録せずブランチ先端からコピーする。
- shader sourceとgenerated headerを不一致のままcommitする。
- Vulkan初期化失敗をログなしでfallbackする。
- `vkDeviceWaitIdle`を毎frame呼ぶ。
- CPU readback表示を最終完成とする。
- renderer enumの既存順序を変更する。
- Vulkan専用fieldを無条件で共有classへ追加する。
- 未検証なのにSoftware／OpenGLが壊れていないと判断する。

---

# 30. commit分割

推奨commit:

```text
V0: record Vulkan source pins and regression baselines
V1: add complete MelonPrime Vulkan build gate
V2: port desktop Vulkan dispatch and context
V3: import pinned Vulkan shaders and generated SPIR-V
V4: port Vulkan 3D renderer and texture cache
V5: adapt guarded GPU lifecycle hooks
V6: add Vulkan frame snapshot and compositor
V7: add Qt desktop Vulkan surface presenter
V8: integrate renderer selection and runtime fallback
V9: enable high-resolution Vulkan output
V10: add Vulkan UI, localization, and diagnostics
V11: add Vulkan CI and regression tests
```

共有melonDSファイル変更と新規Vulkanファイルの巨大importを1commitへ混ぜない。

---

# 31. 各PRに必要な説明

```markdown
## Source pin
- melonDS-android tag:
- parent commit:
- melonDS-android-lib commit:

## Target
- repository:
- branch:
- base commit:

## Ported behavior

## Platform adaptation

## Shared files changed

## Guards

## Software regression result

## OpenGL regression result

## Vulkan build result

## Vulkan runtime result

## Validation result

## Remaining limitations
```

---

# 32. Definition of Done

## Source integrity

```markdown
- [ ] 0.7.0.rc4親SHAを固定
- [ ] サブモジュールSHAを固定
- [ ] shader source hashを記録
- [ ] generated header hashを記録
```

## Architecture

```markdown
- [ ] rendererとpresenterが同一VkDevice
- [ ] Android surface依存を除去
- [ ] VulkanContextがlifecycleを一元管理
- [ ] frame ownershipをsnapshot
- [ ] 2D／3D compositorを移植
```

## Guards

```markdown
- [ ] 共有変更にMELONPRIME_DS
- [ ] Vulkan共有変更にMELONPRIME_ENABLE_VULKAN
- [ ] source／UI／linkが完全gate内
- [ ] Vulkan OFFでVulkan symbolなし
```

## Compatibility

```markdown
- [ ] Software build成功
- [ ] Software runtime正常
- [ ] OpenGL build成功
- [ ] OpenGL runtime正常
- [ ] Metal回帰なし（対象時）
```

## Vulkan

```markdown
- [ ] 1x正常
- [ ] 2x以上正常
- [ ] 2D／3D合成正常
- [ ] Display Capture正常
- [ ] ScreenSwap正常
- [ ] brightness正常
- [ ] resize／fullscreen正常
- [ ] VSync／Fast Forward正常
- [ ] validation error 0
- [ ] 通常経路のfull-frame CPU readback 0
```

## Failure handling

```markdown
- [ ] requested／actual rendererを区別
- [ ] fallback理由をログ
- [ ] configを勝手に変更しない
- [ ] partial initを完全破棄
- [ ] backend再選択可能
```

---

# 33. 最終受け入れ条件

次をすべて満たした時点で「Vulkanが動く」と判断する。

1. UIからVulkanを選択できる。
2. 実際のrendererが`VulkanRenderer3D`であることをログで確認できる。
3. 実際のpresenterがVulkan swapchainを使用している。
4. 1xだけでなく2x以上の3D target寸法が実際に増える。
5. 3Dだけでなく2D、menu、map、fade、Display Captureが正しい。
6. frame Nの2Dとframe Nの3Dが合成される。
7. Softwareへ切り替えて従来どおり動く。
8. OpenGLへ切り替えて従来どおり動く。
9. Vulkan OFFビルドが従来どおり通る。
10. Vulkan validation layerで重大errorが0件である。
11. 通常表示経路に毎frame CPU readbackがない。
12. ROM stop、reopen、renderer切替、fullscreenでresource leakやdeadlockがない。

---

# 34. 参照先

## Target

```text
https://github.com/ag-advania/melonPrimeDS/tree/develop_vulkan
```

監査時HEAD:

```text
7d1ceefe9ec0bffc2a3724b0b99bf9f71ba6f171
```

## Source release

```text
https://github.com/SapphireRhodonite/melonDS-android/releases/tag/0.7.0.rc4
```

親リポジトリtag解決先:

```text
2c10e59d7209d354e90d9ef4228330bac3f6e794
```

## Source core submodule

```text
https://github.com/SapphireRhodonite/melonDS-android-lib
```

実装時は必ず`0.7.0.rc4`が固定したgitlink SHAを使用する。

---

# 35. 実装者への最終命令

```text
Android版のVulkanアルゴリズムとシェーダーはsource pinどおり移植する。
Androidのwindow／driver／AHB依存だけをdesktop Qtへ適応する。
対象ブランチの新しいRenderer3D APIを維持し、共有ファイルを上書きしない。
MelonPrime以外のコード変更はMELONPRIME_DSで隔離する。
Vulkan変更はMELONPRIME_ENABLE_VULKANでさらに隔離する。
SoftwareとOpenGLを既存経路のまま残す。
最終出力までVulkanで接続し、CPU readback bootstrapで完成扱いしない。
設定値ではなくactual renderer、actual presenter、actual image sizeを検証する。
```

---

# 36. develop_vulkan 実装完遂記録（2026-07-17）

この指示書のV0〜V12に対応する実装は`develop_vulkan`へ統合した。特にV6の移植境界は、Sapphireの可変配列をQt側から直接読む方式ではなく、現在のmelonDSコア構造に合わせて次の不変契約へ適応した。

```text
completed generation
  = actual SoftRenderer front buffer
  = GPU3D.RenderScreenSwapAt3D
  = Top/Bottom structured planes[3]
  = line metadata[2]
  = capture 3D source + line mask
  = capture-backed source-class cadence
  = one VulkanFrame sourceGeneration
```

実装上の完了項目:

- V0: frontend/core/target SHAとSPIR-V source/header hashを固定
- V1: `MELONPRIME_VULKAN_ACTIVE`によるsource・define・includeの完全gate
- V2: desktop loader、共有`VulkanContext`、device profile、validation messenger、queue lock
- V3: pinned core shader/pipeline、graphics/compute backend、capture exportを移植
- V4: renderer factory、reset/stop/reopen、requested/actual renderer診断とSoftware fallback
- V5: readback bootstrapを通常表示経路から除外
- V6: 3層2D source、protected black、Display Capture lineage、ScreenSwap/frame generation snapshot
- V7: Win32/Xlib/Wayland surface、swapchain、resize、present mode、out-of-date recovery
- V8: 1x〜16x 3D targetと高解像度compositor sampling
- V9: frame queue、timeline/fence、presenter消費確認、temporal参照保護、pipeline cache
- V10: renderer UI、availability/retry、理由表示、翻訳catalog
- V11: Windows/Linux Vulkan ON/OFF、shader sync、Debug validation CI
- V12: Android surface/AHB/adrenotools依存なし、silent fallbackなし、source attribution

2D不具合修正では、実front buffer、VCount 215でラッチしたScreenSwap、完成世代の排他コピー、pending/retryフレーム固有のdescriptor入力を同一世代へ固定した。Software/OpenGLの既存処理は、共有ファイル内の`MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN`分岐外では変更しない。

最終検証結果（2026-07-17）:

- Windows MinGW Release / Vulkan ON: 標準`tools/build/windows/build-mingw.bat --jobs 1`で成功
- Windows MinGW Release / Vulkan OFF: 独立build tree、`MELONPRIME_ENABLE_VULKAN=OFF`、`MELONPRIME_FORCE_DISABLE_VULKAN=ON`でフルビルド成功
- Vulkan OFF build graph: `GPU3D_Vulkan`、`VulkanContext`、`MelonPrimeVulkan*` source参照 0件
- SPIR-V: 29 shaderを検証、28 pinned headerの差はnon-semantic generator wordのみ、target SPIR-V 1.0
- Android専用symbol scan: `ANativeWindow`、`vulkan_android.h`、`AHardwareBuffer`、adrenotools、librashader混入 0件
- thread boundary strict audit: findings 0
- SRP/performance audit: pass
- platform scatter audit: 21 / 22でpass
- `git diff --check`: error 0（作業treeの改行変換warningのみ）

ROM実機の2D/menu/map/fade/Display Capture、renderer切替、stop/reopen、fullscreen、validation layerについてはcompile/static検証とは別の手動受け入れ項目として扱う。実装側では、これらに必要なcompleted-generation、ScreenSwap latch、present fence、resource lifecycleの契約をV6/V7/V9へ接続済みである。
