# melonPrimeDS Vulkan GPU 3D再構築実装計画
## SapphireRhodonite/melonDS-android 0.7.0.rc4準拠

**作成日:** 2026-07-14  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**参照フロントエンド:** `SapphireRhodonite/melonDS-android` タグ `0.7.0.rc4` `https://github.com/SapphireRhodonite/melonDS-android/releases/tag/0.7.0.rc4`
**参照コア:** `SapphireRhodonite/melonDS-android-lib` コミット `d77944275fa61f9b79cfcead2c3e98993429a023`  
**対象状態:** 現在のVulkanレンダリング経路は破損しているものとして扱う  
**既存計画:** 既存計画書の進行度、完了マーク、phase番号、contract versionを実装済み判定に使わない  

---

# 1. 目的

melonPrimeDSでVulkanを選択した場合に、Nintendo DSの3DポリゴンをVulkan GPUで直接ラスタライズし、そのGPU上の3D画像を2D合成、画面配置、最終表示までGPU常駐で処理する。

最終的な通常フレーム経路は次とする。

```text
GPU3D polygon state
    ↓
GPU3D_AcceleratedFrontend
    ↓
VulkanRenderer3D
    ├─ clear plane
    ├─ clear bitmap
    ├─ texture cache
    ├─ opaque polygons
    ├─ translucent polygons
    ├─ shadow mask / reject / blend
    ├─ toon / highlight
    ├─ depth / stencil
    ├─ edge marking
    ├─ fog
    └─ antialiasing
    ↓
GPU-resident high-resolution 3D image
    ↓
VulkanOutput
    ├─ structured 2D metadata
    ├─ VRAM display
    ├─ FIFO display
    ├─ display capture history
    ├─ screen swap
    └─ master brightness
    ↓
GPU-resident two-screen output
    ↓
FrameQueue
    ↓
VulkanSurfacePresenter
    ↓
Qt native window swapchain
```

Vulkan選択時の3D正解源をSoftware Rendererにしない。

通常表示フレームで次を行わない。

```text
Software 3D framebuffer生成
Software 3D結果のCPUコピー
Software 3D ownership mask生成
CPUによる高解像度再構築
QImageへの3D画像変換
CPU BGRA画面をVulkanへ再アップロード
Vulkan imageの通常フレームreadback
Native Qt painterによる最終表示
```

---

# 2. 現行実装を前提にした再構築方針

現在の`highres_fonts_v3`には、Sapphire由来の`VulkanRenderer3D`本体、structured 2D metadata、`VulkanOutput`、`VulkanSurfacePresenter`、`FrameQueue`が部分的に存在する。

ただし、実際の実行経路は一体化されていない。

現在の構造は概念的に次の状態である。

```text
CreateRendererForSelection(Vulkan)
    ↓
SoftRendererを生成
    ↓
GPU3D::CurrentRendererへVulkanRenderer3Dを別所有
    ↓
Renderer::GetRenderer3D()だけVulkanへ差し替え
    ↓
SoftRendererがVulkanRenderer3D::GetLine()を読む
    ↓
SoftRendererがCPU framebufferを完成
    ↓
ScreenPanelVulkanはNative CPU presenterへ委譲
```

同時に、次の未接続要素が存在する。

```text
VulkanRenderer3D内のSapphireCompositionImage
VulkanRenderer3D内のstructured 2D GPU buffer
Vulkan compositor shader module
VulkanReference/VulkanOutput
VulkanReference/VulkanSurfacePresenter
VulkanReference/FrameQueue
```

この構造を延命しない。

次の原則で整理する。

1. 外側の`VulkanRenderer`を正式な`Renderer`として復活させる。
2. `VulkanRenderer`が`VulkanRenderer3D`、2D metadata producer、`VulkanOutput`を所有する。
3. `GPU3D::CurrentRenderer`によるrenderer横取りを廃止する。
4. `Renderer::GetRenderer3D()`を通常の`Rend3D`所有へ戻す。
5. final compositorを`VulkanRenderer3D`内部に置かない。
6. final compositorは参照実装と同じく`VulkanOutput`へ集約する。
7. `ScreenPanelVulkan`をCPU presenterではなく`VulkanSurfacePresenter`へ接続する。
8. Vulkan Computeという別名で未実装backendを表示しない。
9. phase contractやboolは実装の代用にしない。
10. 参照実装のコードを先に固定し、desktop差分だけをadapterとして追加する。

---

# 3. 最終クラス所有関係

最終所有関係を次へ統一する。

```text
GPU
└─ std::unique_ptr<Renderer> Rend
   └─ VulkanRenderer
      ├─ std::unique_ptr<Renderer2D> Rend2D_A
      ├─ std::unique_ptr<Renderer2D> Rend2D_B
      ├─ std::unique_ptr<Renderer3D> Rend3D
      │  └─ VulkanRenderer3D
      ├─ std::unique_ptr<VulkanOutput> Output
      ├─ FrameQueue
      └─ immutable frame state
```

GUI側は次を所有する。

```text
ScreenPanelVulkan
├─ native surface host
├─ VulkanSurfacePresenter
├─ surface ID
├─ current layout state
├─ current background state
└─ current HUD/OSD state
```

共有GPU objectは次へ集約する。

```text
VulkanContext
├─ VkInstance
├─ VkPhysicalDevice
├─ VkDevice
├─ VkQueue
├─ queue family index
├─ queue mutex
├─ enabled extensions
├─ enabled features
├─ device profile
├─ timeline semaphore functions
├─ descriptor indexing functions
└─ timestamp functions
```

---

# 4. 実装境界

## 4.1 melonDS共通層

次の変更はbackend非依存の最小限に限定する。

```text
src/GPU.h
src/GPU.cpp
src/GPU3D.h
src/GPU3D.cpp
src/GPU_Soft.h
src/GPU_Soft.cpp
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

共通層へ追加するのは次だけとする。

- renderer output種別
- renderer output lease
- structured 2D metadataの抽象interface
- Vulkan renderer生成に必要なbackend-neutral lifecycle hook
- display captureとaccelerated rendererの共通hook
- renderer切替時の安全な所有権移動

Vulkan API型を共通層へ露出する場合は、既存の`MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN`ガード内へ限定する。

## 4.2 MelonPrime Vulkan専用層

次へVulkan固有実装を置く。

```text
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU2D_Vulkan.h
src/GPU2D_Vulkan.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/VulkanDesktopSurface.h
src/VulkanDesktopSurface.cpp
src/VulkanPerfStats.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.h
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/VulkanReference/*
```

## 4.3 参照実装コードとdesktop adapterの分離

参照実装由来コードへQt固有処理を直接混ぜない。

```text
VulkanReference/
    参照実装に近いrenderer output、frame queue、presenter logic

MelonPrimeVulkanSurfaceHost.*
    HWND、XCB/Xlib、Wayland、MoltenVK surfaceの取得

MelonPrimeScreenVulkan.*
    Qt event、window lifecycle、layout、HUD、OSD
```

---


# 4A. AI Sonnet共通実装指示

各フェーズをAI Sonnetへ依頼するときは、そのフェーズの指示だけでなく、この共通指示も同時に渡す。

## 4A.1 作業対象

```text
Repository: ag-advania/melonPrimeDS
Branch: highres_fonts_v3
Reference frontend:
  SapphireRhodonite/melonDS-android
  tag 0.7.0.rc4

Reference core:
  SapphireRhodonite/melonDS-android-lib
  commit d77944275fa61f9b79cfcead2c3e98993429a023
```

作業開始時に現在の`highres_fonts_v3`を読み直し、計画書内の過去のファイル内容を現在の実装だと仮定しないこと。

## 4A.2 実装原則

1. 対象フェーズのコードをリポジトリへ直接実装すること。
2. 関数本体を省略しないこと。
3. 宣言だけ、空実装、常に成功を返す実装、ログだけの実装を残さないこと。
4. `TODO`、`FIXME`、`stub`、`bootstrap`、将来実装予定のboolで完了扱いにしないこと。
5. Software 3DをVulkanの正解源、代替描画源、通常表示源にしないこと。
6. Vulkanの通常フレームをCPU framebuffer、`QImage`、Qt painterへ戻さないこと。
7. 参照実装のclass境界、descriptor ABI、push constant ABI、frame ownershipを優先すること。
8. Android固有のwindow、JNI、ANativeWindow処理はdesktop adapterへ置き換えること。
9. 参照実装のコードへQt UI処理を直接混ぜないこと。
10. 同じ責務をcoreとfrontendの両方へ二重実装しないこと。
11. 同じVulkan resourceを複数classが所有しないこと。
12. backup directoryを作成しないこと。
13. source fileをリポジトリ内の隠しbackup treeへ複製しないこと。
14. 既存backup treeを参照元として使わないこと。
15. renderer名、contract version、phase markerを能力判定に使わないこと。
16. 実際に生成済みのresourceと有効なruntime stateから能力を判定すること。
17. renderer切替、ROM切替、savestate、scale変更で旧generationのresourceを再利用しないこと。
18. `vkDeviceWaitIdle()`を通常フレームへ入れないこと。
19. GPU queue操作は`VulkanContext`のqueue ownership規則へ統一すること。
20. 変更対象外のOpenGL、Metal、Software rendererの挙動を変えないこと。

## 4A.3 melonPrimeDS分離規則

melonDS共通コードを変更する場合は、MelonPrime固有処理を可能な限り次のガード内へ置く。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MelonPrime Vulkan implementation
#endif
```

既存melonDS処理を変更する必要がある場合は、非MelonPrime側の処理を保持する。

```cpp
#ifdef MELONPRIME_DS
    // MelonPrime implementation
#else
    // Existing melonDS implementation
#endif
```

共通interfaceとして必要な変更はbackend-neutralにし、Vulkan API型を無条件に共通headerへ露出しないこと。

## 4A.4 参照実装の扱い

参照実装を移植するときは次を守る。

```text
参照元ファイル
参照commit
移植先ファイル
desktop向け変更点
MelonPrime向け変更点
```

をコードコメントまたは作業報告で追跡可能にする。

参照実装由来のdescriptor binding、push constant、buffer layout、shader variant keyを推測で変更しないこと。

## 4A.5 フェーズ間境界

- 指示されたフェーズより後の大規模責務を先回りして別方式で実装しない。
- 次フェーズに必要なinterfaceは定義してよいが、偽の成功状態を返す仮実装は作らない。
- 既存の壊れた経路を互換目的で残す場合は、通常実行経路から切り離す。
- 同じ実装を旧経路と新経路で併走させない。
- 後続フェーズで削除するtemporary bridgeが必要な場合は、用途と削除フェーズを名前とコメントで明示する。

## 4A.6 Sonnetの作業報告形式

各フェーズの実装後は、次の順で報告すること。

```text
1. 変更したファイル
2. 新しく追加した型、関数、resource
3. 削除した旧経路
4. 所有権とframe lifecycleの変更
5. 参照実装との差分
6. 後続フェーズへ渡すinterface
```

コードを変更していないのに完了と報告しないこと。

---

# 5. フェーズR0: 現在の偽装された完成状態を解除する

## 5.1 shell contractを能力判定に使わない

次を実装判定から外す。

```text
VulkanRendererShellContract
NativeVulkan3DImplemented
SapphireRenderer3DOwnership
SapphireFrameLifecycle
SapphireStructured2DMetadata
SapphirePacked2DGpuUpload
SapphireGpuCompositionResources
SapphireGpuCompositionCommandContext
ContractVersion
```

これらのboolを参照するfactory分岐を削除する。

実際の能力は、初期化済みobjectから得る。

```cpp
struct VulkanRuntimeCapabilities
{
    bool Renderer3DReady = false;
    bool Structured2DReady = false;
    bool FinalCompositorReady = false;
    bool PresenterReady = false;
    bool TimelineSemaphoreReady = false;
    bool DescriptorIndexingReady = false;
    bool ComputeRasterReady = false;
};
```

## 5.2 A1～A7コメントを進行管理に使わない

次の形式のコメントを実行条件にしない。

```text
MELONPRIME_SAPPHIRE_VULKAN_*_A1
MELONPRIME_SAPPHIRE_VULKAN_*_A2
MELONPRIME_SAPPHIRE_VULKAN_*_A3
MELONPRIME_SAPPHIRE_VULKAN_*_A4
MELONPRIME_SAPPHIRE_VULKAN_*_A5
MELONPRIME_SAPPHIRE_VULKAN_*_A6
MELONPRIME_SAPPHIRE_VULKAN_*_A7
```

参照実装へ一致する実体コードへ置き換えた後に削除する。

## 5.3 Vulkan選択中のactual renderer報告を正す

現在のfactoryはVulkan選択時にも`SoftRenderer`を返しているため、`report.actual`をVulkanのままにしない。

再構築中は次のどちらかに限定する。

```text
完全なVulkanRenderer生成成功
    actual = Vulkan

生成失敗
    actual = Software
```

`SoftRenderer + VulkanRenderer3D override`をVulkanとして報告する状態を廃止する。

---


## R0 AI Sonnetへの具体的な実装指示

### 目的

現在のコードにある「実体が接続されていないのにVulkan実装済みと報告する仕組み」を停止する。R0では描画機能を増やさず、runtime報告を実際のobject stateへ一致させる。

### 最初に読むファイル

```text
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/VulkanReferencePortVersion.h
src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp
src/frontend/qt_sdl/MelonPrimeVideoBackend.h
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

リポジトリ全体から次を検索する。

```text
VulkanRendererShellContract
DescribeVulkanRendererShell
NativeVulkan3DImplemented
SapphireRenderer3DOwnership
SapphireFrameLifecycle
SapphireStructured2DMetadata
SapphireGpuComposition
ContractVersion
MELONPRIME_SAPPHIRE_VULKAN_
```

### 実装内容

1. `VulkanRendererShellContract`をfactoryの能力判定から外す。
2. `DescribeVulkanRendererShell()`の戻り値をrenderer選択、fallback、actual renderer報告に使わない。
3. `BackendCreationReport.actual`を実際に生成されたouter `Renderer`の種類へ一致させる。
4. 現時点でfactoryが`SoftRenderer`を返す経路は`actual = renderer3D_Software`として報告する。
5. Vulkan初期化成功を表す情報は、実際のresource stateから作る`VulkanRuntimeCapabilities`へ移行する。
6. `VulkanRuntimeCapabilities`は少なくとも次を持つ。

```cpp
struct VulkanRuntimeCapabilities
{
    bool ContextReady = false;
    bool Renderer3DReady = false;
    bool Structured2DReady = false;
    bool FinalCompositorReady = false;
    bool PresenterReady = false;
    bool TimelineSemaphoreReady = false;
    bool DescriptorIndexingReady = false;
};
```

7. capabilityを設定する関数はresource作成関数の成功後だけ呼ぶ。
8. `NativeVulkan3DImplemented = true`のようなcompile-time既定値を削除または利用停止する。
9. `VulkanReferencePortVersion.h`は参照versionの記録だけに限定し、runtime能力を宣言しない。
10. UIへ表示するrenderer名と内部能力判定を分離する。
11. fallback理由へ「contract boolがfalse」を使わず、実際に失敗した初期化stageを入れる。
12. A1～A7 markerはこの段階で削除しなくてもよいが、制御分岐へ使わない。

### 変更してはいけない範囲

- まだ`GPU3D::CurrentRenderer`を削除しない。
- まだ正式な`VulkanRenderer`を完成させない。
- shader、pipeline、presenterをR0で作り直さない。
- capabilityを常にtrueにする代替structを作らない。

### フェーズ完了時に残す状態

```text
表示上のactual rendererと実際のouter Rendererが一致
shell contractは説明用にも能力判定用にも不要
runtime capabilityはresource実体からのみ更新
A1～A7 markerが実行経路を決定しない
```

---

# 6. フェーズR1: 0.7.0.rc4基準ソースを固定する

## 6.1 固定する参照点

コアは必ず次のcommitを使う。

```text
d77944275fa61f9b79cfcead2c3e98993429a023
```

frontendは必ず次のtagを使う。

```text
0.7.0.rc4
```

移植元をmaster、nightly、後続releaseへ自動追従させない。

## 6.2 コア側の再取り込み対象

次を参照commitとファイル単位で再照合し、MelonPrime固有追加を分離する。

```text
GPU3D_AcceleratedFrontend.h
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.h
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.h
GPU3D_TexcacheVulkan.cpp
VulkanContext.h
VulkanContext.cpp
VulkanPerfStats.h
```

shader sourceとgenerated headerも同じcommitへ揃える。

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

## 6.3 frontend側の再取り込み対象

```text
VulkanOutput.h
VulkanOutput.cpp
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
FrameQueue.h
FrameQueue.cpp
VulkanSurfacePresenter.h
VulkanSurfacePresenter.cpp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanFilterMode.h
```

Android固有部分は削除するのではなく、platform adapterへ置換する。

## 6.4 参照コードへのMelonPrime固有差分の置き方

差分は次の3種類に分ける。

```text
Core compatibility adaptation
Desktop platform adaptation
MelonPrime UI/layout adaptation
```

各差分を別関数、別型、別ファイルへ置き、参照関数の内部へ広範囲に混在させない。

---


## R1 AI Sonnetへの具体的な実装指示

### 目的

0.7.0.rc4と対応するcore commitを唯一の移植基準として固定し、現在混在している別時点のsource、generated shader、MelonPrime独自改変を分類する。

### 最初に読むファイル

```text
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.h
src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp
src/frontend/qt_sdl/VulkanReference/FrameQueue.h
src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
```

### 参照元

```text
SapphireRhodonite/melonDS-android-lib
commit d77944275fa61f9b79cfcead2c3e98993429a023

SapphireRhodonite/melonDS-android
tag 0.7.0.rc4
```

### 実装内容

1. 参照元の対象ファイルを同一commit/tagから取得する。
2. 現行ファイルと参照ファイルを関数単位で比較する。
3. 差分を次へ分類する。

```text
Reference code
Required melonPrime core adapter
Required desktop adapter
Obsolete phase/bootstrap code
Unexplained divergence
```

4. `Unexplained divergence`は参照実装へ戻す。
5. MelonPrime固有差分は可能な限り別関数または別adapter fileへ移す。
6. copyright headerとlicenseを保持する。
7. shader sourceとgenerated headerの組を同じ参照時点へ揃える。
8. shader source hashとgenerated headerの不一致が生じない構成にする。
9. `VulkanReferencePortVersion.h`へ次を定数として記録する。

```cpp
inline constexpr const char* kSapphireFrontendTag = "0.7.0.rc4";
inline constexpr const char* kSapphireCoreCommit =
    "d77944275fa61f9b79cfcead2c3e98993429a023";
```

10. 参照実装からAndroid固有JNI関数をcoreへ持ち込まない。
11. `ANativeWindow`、Java callback、Android loggingはdesktop adapter対象として切り分ける。
12. 参照実装のnamespaceを無計画にmelonDSへ統合しない。
13. 既に`MelonDSAndroid` namespaceで移植されているfrontend codeは、後続移行中にABIを崩さない。
14. backup treeのファイルと比較せず、GitHub上の固定参照だけを基準にする。
15. 対象ファイルごとに参照元pathをコメントまたは移植manifestへ記録する。

### 追加する移植manifest

```text
src/VulkanReferenceManifest.md
```

内容は実装情報だけとし、各ファイルについて次を記載する。

```text
移植先
参照repository
参照path
参照commit/tag
保持したdesktop差分
保持したMelonPrime差分
```

### 変更してはいけない範囲

- 参照実装を現在のmasterへ更新しない。
- Android UI、JNI、activity codeを移植しない。
- 現行の壊れた所有関係を参照実装の仕様として正当化しない。
- generated SPIR-Vだけを別commitから混ぜない。

### フェーズ完了時に残す状態

```text
すべてのVulkan主要ファイルの参照時点が一意
MelonPrime差分とdesktop差分が識別可能
不明な独自改変が参照本体へ混在していない
後続フェーズが固定sourceを基準に実装可能
```

---

# 7. フェーズR2: renderer所有権を正常化する

## 7.1 `GPU3D::CurrentRenderer`を削除する

次を削除する。

```cpp
std::unique_ptr<Renderer3D> GPU3D::CurrentRenderer;
GPU3D::SetCurrentRenderer();
GPU3D::GetCurrentRendererOverride();
```

`GPU3D`はrendererを所有しない。

## 7.2 `Renderer::GetRenderer3D()`を通常所有へ戻す

最終形を次へ戻す。

```cpp
Renderer3D& GetRenderer3D() noexcept
{
    return *Rend3D;
}

const Renderer3D& GetRenderer3D() const noexcept
{
    return *Rend3D;
}
```

Vulkanだけ別経路でrendererを横取りしない。

## 7.3 `GPU::SetRenderer()`の順序を整理する

最終順序を次へ統一する。

```text
旧rendererのcapture同期
旧rendererのStop
旧renderer破棄
新rendererをRendへmove
新renderer Init
新renderer Reset
VRAM cache state reset
palette/OAM dirty state reset
```

`GPU3D::SetCurrentRenderer(nullptr)`を削除する。

## 7.4 renderer切替objectを一度だけ生成する

次の二段生成を廃止する。

```text
CreateRendererForSelection()
CreateRenderer3DOverrideForSelection()
```

factoryは`Renderer`を一つだけ返す。

---


## R2 AI Sonnetへの具体的な実装指示

### 目的

`GPU3D`がrendererを横取りする二重所有を削除し、outer `Renderer`だけが`Renderer3D`を所有する通常のmelonDS構造へ戻す。

### 最初に読むファイル

```text
src/GPU.h
src/GPU.cpp
src/GPU3D.h
src/GPU3D.cpp
src/GPU_Soft.h
src/GPU_Soft.cpp
src/frontend/qt_sdl/MelonPrimeRendererFactory.h
src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

検索対象:

```text
CurrentRenderer
SetCurrentRenderer
GetCurrentRendererOverride
CreateRenderer3DOverrideForSelection
GetRenderer3D()
Start3DRendering
Finish3DRendering
Restart3DRendering
```

### 実装内容

1. `GPU3D`から次を削除する。

```cpp
std::unique_ptr<Renderer3D> CurrentRenderer;
void SetCurrentRenderer(std::unique_ptr<Renderer3D>&&);
Renderer3D* GetCurrentRendererOverride();
const Renderer3D* GetCurrentRendererOverride() const;
```

2. `GPU3D.cpp`のoverride stop/reset処理を削除する。
3. `Renderer::GetRenderer3D()`を`Rend3D`だけ返す実装へ戻す。
4. `GPU::SetRenderer()`から`GPU3D.SetCurrentRenderer(nullptr)`を削除する。
5. `CreateRenderer3DOverrideForSelection()`の宣言、定義、call siteを削除する。
6. `EmuThread::updateRenderer()`内の3D override生成、move、初期化を削除する。
7. outer renderer生成と3D renderer生成を同じfactory transactionへ統合する準備を行う。
8. renderer切替順序を次へ揃える。

```text
capture同期
旧renderer Stop
旧renderer破棄
新renderer move
新renderer Init
新renderer Reset
VRAM cache reset
palette/OAM dirty
```

9. `SoftRenderer`は自身の`Rend3D`として`SoftRenderer3D`だけを所有する。
10. Vulkan選択で`SoftRenderer`へVulkanRenderer3Dを注入する仕組みを残さない。
11. `Renderer3D`のlifecycle hookはouter renderer経由だけで呼ぶ。
12. `Renderer` destructorより後まで`Renderer3D`へのraw pointerを保持しない。

### R3との接続

R2の最終変更では、Vulkan factory callをR3で追加する正式`VulkanRenderer`へ接続できる形にする。

R2単独でVulkanをSoftwareへ偽装し続ける分岐を新設しない。R2とR3を連続作業として扱い、R3のclass skeletonが必要なら同じ変更系列で先に追加する。

### 変更してはいけない範囲

- global/staticなVulkanRenderer3D pointerへ置き換えない。
- `GPU`へ別の`Renderer3D`所有fieldを追加しない。
- `SoftRenderer`へoptional Vulkan childを追加しない。
- raw pointer ownershipへ変更しない。

### フェーズ完了時に残す状態

```text
RendererがRenderer3Dの唯一の所有者
GPU3Dはemulated 3D stateだけを所有
renderer切替で旧VulkanRenderer3Dが確実に破棄
outer rendererと3D rendererのidentityが一致
```

---

# 8. フェーズR3: 正式な`VulkanRenderer`を実装する

## 8.1 class定義

`GPU_Vulkan.h`へ次を実装する。

```cpp
class VulkanRenderer final : public Renderer
{
public:
    explicit VulkanRenderer(
        NDS& nds,
        VulkanRendererMode mode) noexcept;
    ~VulkanRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;

    void PreSavestate() override;
    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void VBlank() override;
    void VBlankEnd() override;

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        bool complete) override;

    bool GetFramebuffers(void** top, void** bottom) override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

private:
    bool InitializeRenderer3D();
    bool Initialize2DProducer();
    bool InitializeOutput();
    void BeginFrame();
    void EndFrame();
    void InvalidateOutputGeneration();
};
```

## 8.2 constructorで所有するobject

```cpp
Rend3D = VulkanRenderer3D::New();
Rend2D_A = std::make_unique<StructuredSoftRenderer2D>(...);
Rend2D_B = std::make_unique<StructuredSoftRenderer2D>(...);
Output = std::make_unique<MelonDSAndroid::VulkanOutput>();
FrameQueue = std::make_unique<MelonDSAndroid::FrameQueue>();
```

`SoftRenderer`を継承しない。

## 8.3 `GetFramebuffers()`

Vulkan通常経路ではRAM framebufferを公開しない。

```cpp
bool VulkanRenderer::GetFramebuffers(void** top, void** bottom)
{
    *top = nullptr;
    *bottom = nullptr;
    return false;
}
```

## 8.4 `GetOutput()`

最終GPU image descriptorを返す。

```cpp
RendererOutput VulkanRenderer::GetOutput()
{
    return RendererOutput::VulkanImage(&PublishedOutput);
}
```

## 8.5 mode

```cpp
enum class VulkanRendererMode
{
    Graphics
};
```

0.7.0.rc4へ合わせる最初の正式backendはGraphicsのみとする。

既存の`renderer3D_VulkanCompute`は独立backendとして扱わない。

次のいずれかへ変更する。

```text
UIから一時的に非表示
Vulkan Graphicsへ正規化
設定migrationでVulkanへ置換
```

未実装Compute選択をGraphicsへ見せかけたまま残さない。

---


## R3 AI Sonnetへの具体的な実装指示

### 目的

Vulkan選択時に`SoftRenderer`ではなく、正式な`VulkanRenderer : Renderer`を生成する。

### 対象ファイル

```text
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU2D_Vulkan.h
src/GPU2D_Vulkan.cpp
src/GPU.h
src/GPU.cpp
src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp
src/frontend/qt_sdl/MelonPrimeRendererFactory.h
src/CMakeLists.txt
```

### class実装

`GPU_Vulkan.h`へ次の責務を持つ`VulkanRenderer`を実装する。

```cpp
class VulkanRenderer final : public Renderer
{
public:
    explicit VulkanRenderer(
        NDS& nds,
        VulkanRendererMode mode) noexcept;
    ~VulkanRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;

    void PreSavestate() override;
    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void VBlank() override;
    void VBlankEnd() override;

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        bool complete) override;

    bool GetFramebuffers(void** top, void** bottom) override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;
};
```

### constructor

1. `Renderer(nds.GPU)`を呼ぶ。
2. `Rend3D`へ`VulkanRenderer3D`を所有させる。
3. Engine A/B用の2D metadata producerを所有させる。
4. `VulkanOutput`と`FrameQueue`のowner fieldを用意する。
5. constructorではVulkan resourceを作成しない。
6. resource作成は`Init()`へ集約する。

### `Init()`

次の順で初期化する。

```text
VulkanContext Acquire
VulkanRenderer3D生成
VulkanRenderer3D Init
2D producer初期化
VulkanOutput初期化
FrameQueue初期化
初期output generation作成
runtime capabilities更新
```

途中で失敗した場合は、作成済みresourceを逆順で解放し、falseを返す。

### `Reset()`

```text
3D renderer reset
2D producer reset
output temporal history reset
frame queue clear
generation increment
published output invalidate
```

### `Stop()`

```text
新規frame publish停止
output lease発行停止
queue内frame retire
VulkanOutput shutdown
VulkanRenderer3D Stop
2D producer shutdown
VulkanContext Release
```

`Stop()`を複数回呼んでも二重破棄しない。

### `DrawScanline()`

1. CPU final framebufferを作らない。
2. Engine A/Bの2D layer evaluationを呼ぶ。
3. structured plane/control metadataをproducerへ渡す。
4. display captureで3D native lineが必要な場合だけ`Rend3D->GetLine()`を使用する。
5. line 0でstructured frameを開始する。
6. line 191でstructured frameをpublishする。

### output API

`GetFramebuffers()`は`top`と`bottom`をnullへし、falseを返す。

`GetOutput()`はGPU output descriptorを返す。

`AcquireOutputLease()`はpublished frameとgenerationを固定し、lease解放までframeを再利用しない。

### factory

```cpp
case renderer3D_Vulkan:
    return std::make_unique<melonDS::VulkanRenderer>(
        nds,
        melonDS::VulkanRendererMode::Graphics);
```

Vulkan選択で`SoftRenderer`を返す分岐を削除する。

### 変更してはいけない範囲

- `VulkanRenderer`を`SoftRenderer`から継承しない。
- CPU framebuffer配列を`VulkanRenderer`へ追加しない。
- `GetFramebuffers()`へreadbackを実装しない。
- `Init()`が失敗しているのにcapabilityをtrueにしない。
- `renderer3D_VulkanCompute`を別backendとして新規実装しない。

### フェーズ完了時に残す状態

```text
Vulkan選択でVulkanRendererがouter renderer
VulkanRendererがRend3Dを所有
Software rendererとの二重所有なし
GPU output APIのownerがVulkanRenderer
```

---

# 9. フェーズR4: `VulkanContext`をdesktop用に完成させる

## 9.1 instance extension

platformごとに必要なinstance extensionを構築する。

### Windows

```text
VK_KHR_surface
VK_KHR_win32_surface
```

### Linux X11

```text
VK_KHR_surface
VK_KHR_xlib_surface
```

または

```text
VK_KHR_surface
VK_KHR_xcb_surface
```

### Linux Wayland

```text
VK_KHR_surface
VK_KHR_wayland_surface
```

### macOS MoltenVK

```text
VK_KHR_surface
VK_EXT_metal_surface
VK_KHR_portability_enumeration
```

instance create flagsへ次を設定する。

```text
VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
```

## 9.2 device extension

必須:

```text
VK_KHR_swapchain
```

利用可能時:

```text
VK_KHR_timeline_semaphore
VK_EXT_descriptor_indexing
VK_EXT_host_query_reset
VK_KHR_portability_subset
```

## 9.3 physical device選択

単純なdiscrete優先だけにしない。

候補ごとに次を確認する。

```text
graphics queue
surface present対応
swapchain extension
必要format
必要depth/stencil format
storage image対応
sampled integer image対応
max image dimension
max descriptor count
timeline semaphore
descriptor indexing
```

surface作成前のrenderer初期化では、graphics capabilityを選び、presenter attach時にpresent capabilityを確認する。

graphics queueとpresent queueが異なる場合を扱う。

```cpp
u32 GraphicsQueueFamilyIndex;
u32 PresentQueueFamilyIndex;
VkQueue GraphicsQueue;
VkQueue PresentQueue;
```

同一familyならsingle queue fast pathを使う。

## 9.4 feature chain

feature queryとdevice create chainを明確に分離する。

```cpp
VkPhysicalDeviceFeatures2 queriedFeatures;
VkPhysicalDeviceTimelineSemaphoreFeatures queriedTimeline;
VkPhysicalDeviceDescriptorIndexingFeatures queriedIndexing;
```

有効化するfeatureだけを別のenabled構造へコピーする。

self-assignment形式を廃止する。

## 9.5 context lifetime

`Acquire()`と`Release()`は次を保証する。

```text
最初のAcquireだけinstance/device生成
失敗時にReferenceCountを0へ戻す
複数renderer/presenterが共有
最後のReleaseだけdevice破棄
presenter leaseが残る間は破棄しない
```

## 9.6 queue access

すべてのsubmitとpresentを共通queue lockで保護する。

```cpp
std::scoped_lock queueLock(VulkanContext::Get().GetQueueLock());
```

長時間のCPU処理中にqueue lockを保持しない。

---


## R4 AI Sonnetへの具体的な実装指示

### 目的

`VulkanContext`をrenderer、output、presenterが共有できるdesktop Vulkan contextへ完成させる。

### 対象ファイル

```text
src/VulkanContext.h
src/VulkanContext.cpp
src/VulkanDesktopCompat.h
src/VulkanDesktopCompat.cpp
src/frontend/qt_sdl/MelonPrimeVulkanInstanceHost.h
src/frontend/qt_sdl/MelonPrimeVulkanInstanceHost.cpp
src/CMakeLists.txt
src/frontend/qt_sdl/CMakeLists.txt
```

### API再設計

`VulkanContext`へ次を追加する。

```cpp
struct VulkanPlatformRequirements
{
    std::vector<const char*> InstanceExtensions;
    VkInstanceCreateFlags InstanceFlags = 0;
};

struct VulkanQueueSelection
{
    u32 GraphicsFamily = VK_QUEUE_FAMILY_IGNORED;
    u32 PresentFamily = VK_QUEUE_FAMILY_IGNORED;
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    VkQueue PresentQueue = VK_NULL_HANDLE;
};
```

context初期化前にplatform requirementsを設定できるAPIを作る。

### instance

1. `volkInitialize()`を最初に呼ぶ。
2. platformごとのsurface extensionを追加する。
3. extensionの存在を列挙結果から確認してから有効化する。
4. macOSではportability enumerationを有効化する。
5. `volkLoadInstance()`をinstance生成直後に呼ぶ。
6. debug messengerはdeveloper buildだけで作成できる構造にする。

### physical device選択

候補ごとに次を評価する。

```text
graphics queue
required device extensions
required image formats
storage image
sampled image
depth/stencil format
descriptor limits
timestamp support
timeline semaphore
descriptor indexing
```

discrete GPU優先点だけで選ばない。

### device feature chain

query用structとenable用structを分離する。

timeline、descriptor indexing、host query resetを利用可能な場合だけenableする。

次のself-assignment形式を残さない。

```cpp
feature.member = feature.member;
```

### queue

1. graphics queueを必須とする。
2. presenter surfaceがattachされた後、present対応familyを解決できるAPIを用意する。
3. graphics/present familyが同じ場合はqueueを共有する。
4. 異なる場合は2 familyのqueue create infoを作る。
5. `GetQueueLock()`をgraphics submitとpresent submitで共有する。
6. queue lockをCPU scene build中に保持しない。

### lifetime

`Acquire()`失敗時はreference countを0へ戻す。

`Release()`は最後のownerだけdeviceを破棄する。

`shutdownLocked()`は次の順序にする。

```text
queue idleまたは所有frame完了
device resource owner解放済み確認
device destroy
instance surfaceが全て破棄済み
instance destroy
volk state reset
```

### platform profile

`VulkanDeviceProfile`へdesktop vendor情報を正しく設定する。

```text
NVIDIA
AMD
Intel
Qualcomm
ARM Mali
PowerVR
Apple/MoltenVK
```

vendor固有pathをrenderer側で選べるgetterを用意する。

### 変更してはいけない範囲

- rendererごとに別instance/deviceを作らない。
- presenter側で独自deviceを作らない。
- Windows extensionだけを常時要求しない。
- surface未作成を理由にgraphics renderer初期化を失敗させない。
- device破棄前にlive image/viewを残さない。

### フェーズ完了時に残す状態

```text
core rendererとfrontend presenterが同じVkDeviceを共有
Windows、X11/XCB、Wayland、MoltenVK用extensionを選択可能
graphics queueとpresent queueを表現可能
feature有効化状態をruntimeから取得可能
```

---

# 10. フェーズR5: shader生成経路を一つにする

## 10.1 sourceを正とする

`.comp`、`.vert`、`.frag`を正とし、generated headerを自動生成する。

手編集したSPIR-V配列を正にしない。

## 10.2 generator

```text
tools/vulkan/generate_sapphire_spirv.py
```

へ次を実装する。

```text
glslangValidatorの解決
source一覧読込
define variant生成
SPIR-V生成
C++ header生成
source hash記録
ABI manifest生成
```

## 10.3 core shader一覧

参照commitのshaderをそのまま生成対象にする。

graphics shaderとcompute shaderを同じmanifestへ置く。

## 10.4 compositor shader

現在のinline shader dataとA6/A7専用moduleを廃止し、参照frontendと同じgenerated headerへ統一する。

```text
VulkanCompositorShaderData.h
VulkanAccumulate3dShaderData.h
VulkanSurfacePresenterVertexShaderData.h
VulkanSurfacePresenterFragmentShaderData.h
```

## 10.5 ABI定義

descriptor bindingとpush constantをshader側、core側、frontend側で重複定義しない。

共通headerへ集約する。

```cpp
struct VulkanCompositorAbi
{
    static constexpr u32 OutputImage = 0;
    static constexpr u32 Current3DImage = 1;
    static constexpr u32 TopPackedBuffer = 2;
    static constexpr u32 BottomPackedBuffer = 3;
    static constexpr u32 PreviousTop3DImage = 4;
    static constexpr u32 Capture3DBuffer = 5;
    static constexpr u32 PreviousBottom3DImage = 6;
};
```

---


## R5 AI Sonnetへの具体的な実装指示

### 目的

Vulkan shaderの正本をGLSL sourceへ統一し、参照commitと一致するgenerated SPIR-V headerを一つの生成経路から作る。

### 対象ファイル

```text
src/GPU3D_Vulkan_*Shader.comp
src/GPU3D_Vulkan_*Shader.vert
src/GPU3D_Vulkan_*Shader.frag
src/GPU3D_Vulkan_*ShaderData.h
src/frontend/qt_sdl/VulkanReference/*Shader.*
src/frontend/qt_sdl/VulkanReference/*ShaderData.h
src/CMakeLists.txt
src/frontend/qt_sdl/CMakeLists.txt
tools/vulkan/
```

### generator実装

```text
tools/vulkan/generate_sapphire_spirv.py
```

を追加または再構築する。

generatorは次を行う。

1. shader manifestを読む。
2. `glslangValidator`または指定compilerを解決する。
3. stageごとのentry pointを指定する。
4. variant defineを展開する。
5. `.spv`を生成する。
6. `uint32_t`配列のC++ headerを生成する。
7. byte size、word count、source hashをheaderへ埋め込む。
8. manifest全体のABI hashを生成する。
9. 同じ入力から決定的な出力を作る。
10. 生成物のnamespaceとsymbol名を固定する。

### manifest

```text
tools/vulkan/sapphire_shader_manifest.json
```

へ次を記載する。

```text
source path
stage
entry point
generated header path
symbol name
defines
reference repository
reference commit/tag
```

### shader ABI共通化

次を共通headerへ移す。

```text
descriptor binding constants
push constant size
buffer stride constants
image layer constants
variant bit definitions
```

候補:

```text
src/VulkanShaderAbi.h
src/frontend/qt_sdl/VulkanReference/VulkanCompositorAbi.h
```

coreとfrontendで同じ定数を重複定義しない。

### CMake

1. generated shader targetを一つ作る。
2. coreとfrontendをそのtargetへ依存させる。
3. source変更時だけ再生成する。
4. build directory内へ生成する。
5. source treeの古い手編集headerをincludeしない。
6. generated include directoryを明示する。
7. platformごとにshader一覧を変えない。

### 現行A6/A7 shader data

inline配列、個別に埋め込んだcompositor SPIR-V、ODR回避用複製を削除し、generated symbol一つへ統一する。

### 変更してはいけない範囲

- SPIR-V byte列を手編集しない。
- coreとfrontendで同じshaderを別symbolとして生成しない。
- shader sourceとgenerated headerを別commitから混ぜない。
- push constant sizeをC++側だけ変更しない。
- buildごとにsymbol名が変化する生成方式にしない。

### フェーズ完了時に残す状態

```text
GLSL sourceが唯一の正本
全generated headerが同じgenerator経由
coreとfrontendのABI定数が共有
A6/A7専用inline shader複製が不存在
```

---

# 11. フェーズR6: `VulkanRenderer3D`を参照実装へ戻す

## 11.1 constructor API

現在のMelonPrime版で追加された`GPU3D&` constructor依存を整理する。

参照実装のAPIへ近づける。

```cpp
static std::unique_ptr<VulkanRenderer3D> New() noexcept;
VulkanRenderer3D() noexcept;
```

melonPrimeDS側の`Renderer3D`基底が`GPU3D&`を要求する場合は、factory adapterで渡す。

参照本体の内部へMelonPrime固有所有権処理を入れない。

## 11.2 lifecycle

次を参照順序のまま接続する。

```text
Reset
VCount144
RenderFrame
RestartFrame
SetupAccelFrame
PrepareCaptureFrame
BeginCaptureFrame
SetCaptureScreenSwapHint
Blit
Stop
```

現在のinline adapterで`RenderFrame()`冒頭に`BeginCaptureFrame()`を強制する構造を見直す。

capture lifecycleはGPUのscanline/frame lifecycle側から参照実装と同じ順序で呼ぶ。

## 11.3 backend mode

0.7.0.rc4準拠では次を正式backendにする。

```cpp
BackendMode::GraphicsHardware
```

Compute pipelineはgraphics backend内部の補助実装として扱い、UI上の別renderer identityにしない。

## 11.4 final composition責務を削除する

`VulkanRenderer3D`から次を削除する。

```text
SapphireVulkanCompositionInput
SapphireVulkanCompositionResources
SapphireVulkanCompositionCommandContext
SapphireVulkanCompositionShaderModule
SapphireCompositionImage
SapphireCompositionDescriptorSetLayout
SapphireCompositionDescriptorPool
SapphireCompositionDescriptorSet
SapphireCompositionPipelineLayout
SapphireCompositionCommandPool
SapphireCompositionCommandBuffer
SapphireCompositionFence
ensureSapphireComposition*
destroySapphireComposition*
```

3D rendererが所有するのは3D targetとcapture exportまでとする。

## 11.5 structured 2D責務を移動する

`VulkanRenderer3D`から次を`VulkanRenderer`または`VulkanOutput`へ移す。

```text
Structured2DPlane0
Structured2DPlane1
Structured2DControl
Structured2DNativeFinal
Structured2DLineMeta
Structured2DLineState
Structured2DPacked
Structured2DGpuBuffer
Structured2DGpuMemory
BeginStructured2DFrame
SubmitStructured2DLine
EndStructured2DFrame
```

`VulkanRenderer3D`はstructured 2Dを所有しない。

---


## R6 AI Sonnetへの具体的な実装指示

### 目的

`VulkanRenderer3D`を純粋な3D rendererへ戻し、2D metadataとfinal compositorの所有を外側へ移す。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU2D_Vulkan.h
src/GPU2D_Vulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.h
src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp
```

### 削除対象

`GPU3D_Vulkan.*`から次を削除する。

```text
SapphireVulkanCompositionInput
SapphireVulkanCompositionResources
SapphireVulkanCompositionCommandContext
SapphireVulkanCompositionShaderModule
SapphireCompositionPushConstants
SapphireCompositionDescriptorAbi
SapphireCompositionImage
SapphireCompositionMemory
SapphireCompositionImageView
SapphireCompositionSampler
SapphireCompositionDescriptorSetLayout
SapphireCompositionDescriptorPool
SapphireCompositionDescriptorSet
SapphireCompositionPipelineLayout
SapphireCompositionCommandPool
SapphireCompositionCommandBuffer
SapphireCompositionFence
ensureSapphireComposition*
destroySapphireComposition*
```

structured 2D storageとupload resourceも3D rendererから外す。

### `VulkanRenderer3D`に残す責務

```text
accelerated scene build
3D texture cache
3D render target
graphics/compute pipeline
3D command submission
capture line export
3D image layout/state
3D frame identity
```

### handoff API

`VulkanRenderer3D`から外側へ次だけ公開する。

```cpp
struct Vulkan3DFrameView
{
    VkImage ColorImage = VK_NULL_HANDLE;
    VkImageView ColorImageView = VK_NULL_HANDLE;
    VkImageLayout ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    u32 Width = 0;
    u32 Height = 0;
    u32 Scale = 1;
    u64 FrameSerial = 0;
    VkSemaphore CompletionSemaphore = VK_NULL_HANDLE;
    u64 CompletionValue = 0;
    bool Valid = false;
};
```

必要ならdepth/attributeはdebug APIだけに限定し、final compositorへ公開しない。

### lifecycle API

参照実装の次のAPIを維持する。

```text
Reset
VCount144
RenderFrame
RestartFrame
GetLine
SetupAccelFrame
PrepareCaptureFrame
BeginCaptureFrame
SetCaptureScreenSwapHint
Blit
Stop
```

MelonPrime側の基底class adapterは薄いwrapperにする。

### constructor

参照実装のconstructor形状へ近づける。

melonPrimeDSの`Renderer3D`基底が`GPU3D&`を必要とする場合は、その参照だけを保持し、final compositor ownershipを持ち込まない。

### 移動先

- structured 2D CPU frame: `VulkanStructured2DProducer`
- structured 2D GPU buffer: `VulkanOutput`
- final composition resource: `VulkanOutput`
- frame ownership: `FrameQueue`
- window presentation: `VulkanSurfacePresenter`

### 変更してはいけない範囲

- 削除したcompositorを別名で`VulkanRenderer3D`へ残さない。
- 3D rendererがtop/bottom final screen imageを所有しない。
- 3D rendererがQt layoutを認識しない。
- temporary bridgeを通常フレームの最終outputにしない。
- structured 2Dを3D texture cacheへ混ぜない。

### フェーズ完了時に残す状態

```text
VulkanRenderer3Dは3D targetまでを担当
final compositorはVulkanOutputだけが担当
structured 2Dは外側producer/outputが担当
3D frame handoffが明示的なimmutable view
```

---

# 12. フェーズR7: 3D render targetを完成させる

## 12.1 target size

```text
Width  = 256 × ScaleFactor
Height = 192 × ScaleFactor
```

scale変更時は3D targetを再生成する。

presenter側だけで拡大しない。

## 12.2 image構成

```text
ColorImage
AttrImage
DepthImage
DepthStencilImage
CaptureReadbackImage
```

用途を次へ揃える。

### ColorImage

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
TRANSFER_DST
```

### AttrImage

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
```

### DepthImage

```text
STORAGE
SAMPLED
TRANSFER_SRC
```

### DepthStencilImage

```text
DEPTH_STENCIL_ATTACHMENT
TRANSFER_SRC
```

## 12.3 format

colorとattributeは参照実装のformatを維持する。

depth/stencilはdevice対応から選択する。

```text
D32_SFLOAT_S8_UINT
D24_UNORM_S8_UINT
D16_UNORM_S8_UINT
```

## 12.4 image ownership

3D targetをpresenterへ直接貸さない。

`VulkanOutput`が3D targetを入力として参照し、別のframe output imageへ合成する。

---


## R7 AI Sonnetへの具体的な実装指示

### 目的

内部解像度へ対応したGPU 3D render target一式を、参照実装の用途とlayoutへ揃えて実装する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
```

### 実装するresource

```text
ColorImage
ColorImageMemory
ColorImageView

AttrImage
AttrImageMemory
AttrImageView

DepthImage
DepthImageMemory
DepthImageView

DepthStencilImage
DepthStencilImageMemory
DepthStencilImageView

CaptureReadbackImage
CaptureReadbackMemory
```

### target寸法

```cpp
width = 256u * ScaleFactor;
height = 192u * ScaleFactor;
```

`ScaleFactor`は1以上へclampする。

scale変更時は新targetを作成してから旧targetをretireする。presenterが参照中のimageを即時破棄しない。

### image usage

`ColorImage`:

```text
VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
VK_IMAGE_USAGE_STORAGE_BIT
VK_IMAGE_USAGE_SAMPLED_BIT
VK_IMAGE_USAGE_TRANSFER_SRC_BIT
VK_IMAGE_USAGE_TRANSFER_DST_BIT
```

`AttrImage`:

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
```

`DepthImage`:

```text
STORAGE
SAMPLED
TRANSFER_SRC
TRANSFER_DST
```

`DepthStencilImage`:

```text
DEPTH_STENCIL_ATTACHMENT
TRANSFER_SRC
TRANSFER_DST
```

### format選択

1. color/attrは参照実装と同一formatを使う。
2. depth/stencilはdevice format propertiesから選ぶ。
3. 選択結果を`VulkanRenderer3D`初期化stateへ保存する。
4. render pass、framebuffer、pipeline作成へ同じformatを渡す。
5. format変更時は関連pipelineを再作成する。

### memory

1. device-local memoryを使用する。
2. memory type選択は`VulkanContext::FindMemoryType()`へ統一する。
3. allocation failure時に途中resourceを全て解放する。
4. imageとmemoryの所有を同じclassへ固定する。
5. image view破棄後にimageを破棄し、その後memoryを解放する。

### layout tracking

各imageの現在layoutをfieldで管理する。

barrier関数を共通化する。

```cpp
void TransitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess);
```

利用するVulkan versionに合わせ、同期2未対応なら旧barrierへadapterする。

### clear

target作成直後に未定義pixelをsampleしないよう、初回利用時に全attachmentを明示clearする。

### 変更してはいけない範囲

- presenter拡大だけでScaleFactorを実装しない。
- CPU配列を高解像度3D targetとして保持しない。
- ColorImageをfinal two-screen outputと兼用しない。
- framebuffer再作成時にdevice全体をidleにしない。
- depth valueを8-bit colorへ詰めて代用しない。

### フェーズ完了時に残す状態

```text
内部解像度に応じた実サイズの3D image
color、attribute、depth、stencilがGPU resource
layoutとlifetimeがrenderer内で一元管理
Vulkan3DFrameViewがColorImageを公開可能
```

---

# 13. フェーズR8: render context ringを完成させる

## 13.1 context数

参照実装と同じ6 contextを基本とする。

```cpp
static constexpr size_t AsyncRenderContextCount = 6;
```

## 13.2 contextごとのresource

```text
command pool
command buffer
frame fence
descriptor set
graphics descriptor set
single texture descriptor sets
triangle buffer
graphics vertex buffer
bin mask buffer
group list buffer
span setup buffer
work offset buffer
toon buffer
clear buffer
capture line buffer
timestamp query pool
descriptor cache
```

## 13.3 再利用

次のcontextだけ待つ。

```text
次に再利用するcontext
capture readback source context
texture cache mutationと競合するcontext
```

通常フレームで`vkDeviceWaitIdle()`を呼ばない。

## 13.4 buffer growth

必要capacityを超えた場合だけ再確保する。

```cpp
newCapacity = max(requiredCapacity, oldCapacity * 2);
```

persistent mapped memoryを利用する。

---


## R8 AI Sonnetへの具体的な実装指示

### 目的

複数フレームを安全に処理するrender context ringを実装し、単一command bufferと全体待機への依存を除去する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
```

### context構造

参照実装に合わせ、`RenderContext`へ次を所有させる。

```text
VkCommandPool
VkCommandBuffer
VkFence FrameFence
VkDescriptorSet
VkDescriptorSet GraphicsDescriptorSet
single texture descriptor sets
TriangleBuffer
GraphicsVertexBuffer
BinMaskBuffer
GroupListBuffer
SpanSetupBuffer
WorkOffsetBuffer
ToonBuffer
ClearBuffer
CaptureLineBuffer
TimestampQueryPool
descriptor cache
```

### context数

```cpp
static constexpr size_t AsyncRenderContextCount = 6;
std::array<RenderContext, AsyncRenderContextCount> RenderContexts;
```

### acquire

`AcquireRenderContext()`を実装する。

```text
NextRenderContextIndexを選択
そのcontextのfenceだけ待つ
fence reset
command pool reset
descriptor cacheのresource identity確認
mapped buffer使用範囲reset
contextをcurrentとして返す
NextRenderContextIndex更新
```

### buffer確保

各bufferへcapacity fieldを持たせる。

必要量がcapacityを超えた場合だけ再確保する。

```cpp
newCapacity = std::max(required, oldCapacity == 0 ? required : oldCapacity * 2);
```

host-visible bufferはpersistent mapする。

non-coherent memoryの場合は書込範囲をflushする。

### descriptor cache

resource handleが変化したbindingだけ更新する。

毎frame全descriptorを書き直さない。

texture descriptor countが減った場合に古いdescriptorを参照しないようfallback descriptorで埋める。

### submit

1. command bufferをendする。
2. queue lockを取得する。
3. 3D dependency semaphoreをwaitへ設定する。
4. context fence付きでsubmitする。
5. completion semaphore/timeline valueを発行する。
6. queue lockを解放する。
7. submitted contextをframe identityへ関連付ける。

### timestamp

対応deviceだけquery poolを作る。

timestamp queryがないdeviceでもrender pathを変えない。

### 変更してはいけない範囲

- 全contextを毎frame待たない。
- 単一global command poolへ戻さない。
- mapped pointerをbuffer再確保後も保持しない。
- context fenceをpresent fenceとして兼用しない。
- descriptor setを複数in-flight frameで書き換えない。

### フェーズ完了時に残す状態

```text
6 contextが独立resourceを所有
再利用対象contextだけ待機
bufferはcapacity growth
in-flight descriptor上書きなし
3D completionをframe handoffへ渡せる
```

---

# 14. フェーズR9: accelerated sceneを正式入力にする

## 14.1 共通scene builder

`GPU3D_AcceleratedFrontend`をgraphics pathの唯一のgeometry sourceにする。

別のMelonPrime packed vertex builderを併走させない。

## 14.2 scene内容

```text
Vertices
Indices
EdgeIndices
Triangles
Draws
FirstTranslucentDraw
```

各drawへ次を持たせる。

```text
source polygon
render key
flags
poly attr
poly ID
alpha
primitive type
coverage state
first vertex
vertex count
first index
index count
first edge index
edge index count
first triangle
triangle count
```

## 14.3 source order

`RenderPolygonRAM`順を保持する。

同一pipeline stateでも非隣接polygonをまとめない。

```text
A B A
```

は3 draw rangeのままとする。

## 14.4 line polygon

参照実装のline解決とquad化を使用する。

MelonPrime独自の別line expansionを重ねない。

## 14.5 high-resolution coordinates

```text
ScaleFactor
BetterPolygons
HiresCoordinates
coverage fix
```

をscene build configへ一括で渡す。

`HiresCoordinates`を捨てない。

現在の`SetRenderSettings()`内の`(void)hiresCoordinates`を廃止する。

---


## R9 AI Sonnetへの具体的な実装指示

### 目的

`GPU3D_AcceleratedFrontend`がVulkan graphics pathの唯一のgeometry変換源になるよう統合する。

### 対象ファイル

```text
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D.h
src/GPU3D.cpp
```

### scene ABI

参照実装の`AcceleratedScene`、vertex、triangle、draw layoutを維持する。

sceneへ最低限次を含める。

```text
Vertices
Indices
EdgeIndices
Triangles
Draws
FirstTranslucentDraw
```

### build input

`GPU3D`から次をimmutable inputとして渡す。

```text
RenderPolygonRAM
RenderNumPolygons
RenderDispCnt
RenderAlphaRef
RenderClearAttr1
RenderClearAttr2
RenderFogColor
RenderFogOffset
RenderFogShift
RenderFogDensityTable
RenderToonTable
RenderEdgeTable
RenderXPos
```

### build config

```cpp
struct AcceleratedSceneBuildConfig
{
    u32 ScaleFactor;
    bool BetterPolygons;
    bool HiresCoordinates;
    bool ConservativeCoverageEnabled;
    float ConservativeCoveragePixels;
    float ConservativeCoverageDepthBias;
    bool CoverageRepeat;
    bool CoverageClamp;
};
```

現在捨てられている`HiresCoordinates`をscene builderへ渡す。

### ordering

1. `RenderPolygonRAM`順を保持する。
2. opaqueとtranslucentの境界は参照実装のsort/orderを使う。
3. pipeline stateが同じでも非隣接drawを結合しない。
4. shadow maskとshadow blendの順序を保持する。
5. line polygon、triangle、quadのprimitive変換を参照実装へ揃える。

### vertex conversion

次を参照精度で変換する。

```text
viewport transform
clip result
high-resolution coordinate
reciprocal W
Z/W depth
5-bit vertex color
texture coordinate
polygon attributes
```

floatへの変換位置を参照実装から動かさない。

### draw key

pipeline variantへ必要なstateを`RenderKey`またはflagsへ格納する。

```text
W-buffer
depth equal
alpha
polygon mode
texture mode
toon/highlight
fog
shadow
edge
coverage
```

### upload

scene build後にcurrent `RenderContext`のmapped bufferへ一度でcopyする。

drawごとのsmall uploadを行わない。

### 変更してはいけない範囲

- Vulkan専用の第二scene builderを追加しない。
- SoftwareRenderer3Dのpolygon loopをコピーしない。
- source polygon pointerをGUI threadへ渡さない。
- non-adjacent drawをstate sortで並べ替えない。
- high-resolution設定をpresenterへ委譲しない。

### フェーズ完了時に残す状態

```text
GPU3D_AcceleratedFrontendが唯一のgeometry frontend
VulkanRenderer3DはAcceleratedSceneだけをupload
polygon orderとDS attributeがsceneに保持
HiresCoordinatesとScaleFactorがgeometryへ反映
```

---

# 15. フェーズR10: Vulkan texture cacheを参照実装へ揃える

## 15.1 loader

```cpp
class TexcacheVulkanLoader
{
public:
    TextureHandle GenerateTexture(
        u32 width,
        u32 height,
        u32 layers);

    void UploadTexture(
        TextureHandle handle,
        u32 width,
        u32 height,
        u32 layer,
        void* data);

    void DeleteTexture(TextureHandle handle);

    bool GetTextureDescriptor(
        TextureHandle handle,
        VkDescriptorImageInfo* out) const;

    bool IsTextureLayerOpaque(
        TextureHandle handle,
        u32 layer) const;
};
```

## 15.2 cache更新

```text
VRAMDirty_Texture
VRAMDirty_TexPal
VRAMMap_Texture
VRAMMap_TexPal
```

を使い、変更textureだけを更新する。

## 15.3 descriptor path

device能力に応じて次を選ぶ。

```text
NonUniform
CompatDynamicUniform
BaseSingleDescriptor
```

いずれもGPU rasterを使う。

descriptor indexing非対応をSoftware fallback理由にしない。

## 15.4 sampler

S/T軸のclamp、repeat、mirror組み合わせを共有sampler tableへする。

drawごとにsamplerを生成しない。

## 15.5 fallback texture

白1×1 textureをrenderer初期化時に一度作る。

texture decode失敗polygonを削除せず、untexturedまたはfallback textureとして描画する。

---


## R10 AI Sonnetへの具体的な実装指示

### 目的

DS textureとpaletteをVulkan imageへ変換し、polygon shaderがSoftware texture decodeへ依存せずsampleできるtexture cacheを完成させる。

### 対象ファイル

```text
src/GPU3D_Texcache.h
src/GPU3D_Texcache.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU.h
src/GPU.cpp
```

### loader API

参照実装の`TexcacheVulkanLoader`相当を完成させる。

```text
GenerateTexture
UploadTexture
DeleteTexture
GetTextureDescriptor
IsTextureLayerOpaque
```

### texture resource

各cache entryへ次を持たせる。

```text
VkImage
VkDeviceMemory
VkImageView
width
height
layer count
format
current layout
descriptor info
opaque mask
generation
last used frame
```

### dirty tracking

```text
VRAMDirty_Texture
VRAMDirty_TexPal
VRAMMap_Texture
VRAMMap_TexPal
```

を使い、変更されたtexture/paletteだけ再decodeする。

VRAM mapping generationをcache keyへ含める。

### upload path

1. decode結果をstaging ringへ書く。
2. non-coherent rangeをflushする。
3. imageをtransfer dstへ遷移する。
4. 必要layerへcopyする。
5. shader read layoutへ遷移する。
6. descriptor cacheへ新しいview/layoutを通知する。

### descriptor mode

runtime capabilityから次を選ぶ。

```text
NonUniform
CompatDynamicUniform
BaseSingleDescriptor
```

3 modeで同じtexture decode結果を使う。

descriptor indexing非対応でもVulkan GPU rasterを継続する。

### sampler cache

S/Tの次の組をkeyにする。

```text
clamp
repeat
mirror
nearest/linear
```

同じkeyのsamplerを共有する。

### opaque情報

texture decode時にlayer単位でalphaが常にopaqueか記録する。

NeedOpaque passの省略判断へ利用する。

### fallback texture

renderer初期化時に1×1白textureを作る。

invalid texture entryはfallback descriptorを使う。

polygonをsceneから削除しない。

### eviction

in-flight frameが参照中のtexture resourceを破棄しない。

retire queueへ入れ、completion value到達後に破棄する。

### 変更してはいけない範囲

- 毎draw textureをdecodeしない。
- textureをCPU framebufferとして保持しない。
- descriptor indexing非対応時にSoftware rendererへ切り替えない。
- texture更新のたびに`vkDeviceWaitIdle()`しない。
- cache entryのVkImageを複数ownerで破棄しない。

### フェーズ完了時に残す状態

```text
全polygon textureがVulkan image
dirty regionだけ更新
3 descriptor pathがGPU描画を維持
NeedOpaque用opaque情報を提供
in-flight texture lifetimeが安全
```

---

# 16. フェーズR11: clear planeとclear bitmapを実装する

## 16.1 clear plane

`RenderClearAttr1`と`RenderClearAttr2`から次を生成する。

```text
RGB
alpha
depth
opaque polygon ID
fog flag
stencil
```

render passのclear valueへ反映する。

## 16.2 clear bitmap

`RenderDispCnt`のclear bitmap bitに従う。

```text
VRAM texture region
    ↓
color bitmap
depth/fog bitmap
    ↓
scaled clear pass
```

offsetは`RenderXPos`とclear attributeから参照実装どおりに反映する。

## 16.3 CPU snapshotを作らない

次を行わない。

```text
毎フレーム256×256をstd::vectorへdecode
presenter用snapshotへclear bitmapを複製
QImageへ変換
```

texture cacheまたはdedicated staging resourceからrendererのGPU imageへ直接送る。

---


## R11 AI Sonnetへの具体的な実装指示

### 目的

DS 3Dのclear planeとclear bitmapをGPU render targetへ正しく反映する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
関連clear shader source
```

### clear state

`RenderClearAttr1`と`RenderClearAttr2`から次を展開する。

```text
clear RGB
clear alpha
clear depth
opaque polygon ID
fog flag
stencil initial value
```

`ClearGpuState`のような明示structへまとめる。

### clear plane

1. render pass beginのclear valuesへcolor/depth/stencilを設定する。
2. attribute imageもpolygon ID/fog stateでclearする。
3. scale後の全pixelへ同じDS clear stateを適用する。
4. debug clear色はdeveloper optionが明示された場合だけ上書きする。
5. clear stateをCPU line cacheへ生成しない。

### clear bitmap

1. clear bitmap enable bitを`RenderDispCnt`から取得する。
2. clear bitmap source VRAMとoffsetを参照実装どおり決定する。
3. colorとdepth/fog sourceをdecodeする。
4. dedicated Vulkan textureまたはtexture cache entryへuploadする。
5. full-screen graphics clear passでscaled targetへ描画する。
6. wrap、offset、alpha、depth、poly ID、fog flagをshaderへ渡す。
7. clear bitmap無効時はresource uploadを行わない。
8. source generationが同じ場合は既存GPU imageを再利用する。

### pipeline

clear planeとclear bitmapのpipelineを分離する。

clear bitmap shaderは通常polygon shaderへ混ぜない。

### RenderXPos

clear bitmapのX offsetとcapture/scanline semanticsへ`RenderXPos`を正しく反映する。

### 変更してはいけない範囲

- 256×256 clear bitmapを毎frame`std::vector`へ複製しない。
- Qt presenterでclear bitmapを合成しない。
- clear depthをcolor alphaへ代用しない。
- clear bitmapをSoftwareRenderer3Dから取得しない。
- clear bitmap有効時に通常clear planeだけで済ませない。

### フェーズ完了時に残す状態

```text
clear planeが全attachmentへ反映
clear bitmapがGPU pass
offset、depth、poly ID、fog stateを保持
Software 3D clear結果へ依存しない
```

---

# 17. フェーズR12: opaque polygon pathを完成させる

## 17.1 pipeline variant

最低限次をvariant keyへ含める。

```text
Z-buffer / W-buffer
depth less / depth equal
texture有無
modulate / decal / toon / highlight
fog write
line / triangle
fragment depth path
descriptor indexing path
```

## 17.2 vertex input

参照実装の`GraphicsVertexGpu` ABIを使う。

```text
position
depth
reciprocal W
texture coordinate
vertex color
polygon flags
texture descriptor index
texture size
texture param
polygon attr
```

## 17.3 opaque fragment処理

```text
vertex color補間
texture sampling
texture combiner
alpha test
depth test
depth write
color write
attribute write
polygon ID write
fog flag write
stencil write
```

## 17.4 NeedOpaque

半透明polygon内の完全不透明fragmentを先行opaque passへ送る。

texture arrayのopaque情報を利用し、不要なpassを省く。

---


## R12 AI Sonnetへの具体的な実装指示

### 目的

opaque polygonをVulkan graphics pipelineで直接描画し、color、depth、attribute、stencilをGPU上で生成する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_AcceleratedFrontend.*
graphics raster vertex shader
graphics raster fragment shader variants
graphics no-color fragment shader
```

### pipeline key

次をkeyへ含める。

```text
Z-buffer / W-buffer
depth less / depth equal
depth write
texture enabled
texture combine mode
toon / highlight
fog write
line polygon
fragment depth path
descriptor indexing path
coverage mode
```

pipeline array index計算を一つの関数へ集約する。

### render pass

opaque passでは次をattachmentとして使う。

```text
ColorImage
AttrImage
DepthStencilImage
必要な補助attachment
```

load/store operationをpass目的へ合わせる。

### draw recording

1. accelerated sceneのopaque draw rangeを順番に処理する。
2. pipeline変更時だけbindする。
3. texture descriptor変更時だけbindする。
4. vertex/index bufferを一度bindする。
5. draw rangeごとに`vkCmdDraw`または`vkCmdDrawIndexed`する。
6. polygon ID、flags、texture indexをvertex dataまたはpush constantから渡す。
7. draw順を変更しない。

### fragment処理

参照shaderの順序を保持する。

```text
texture sample
vertex color combine
toon/highlight
alpha evaluation
alpha test
depth calculation
color output
attribute output
polygon ID
fog flag
```

### NeedOpaque

1. translucent polygonの完全不透明fragmentを先行passへ分離する。
2. texture opaque情報からpassの必要性を決める。
3. NeedOpaque drawをsource orderに沿って処理する。
4. alpha fragmentを後続translucent passへ残す。
5.同一polygonを全面二重描画しない。

### UI overlay opaque path

参照実装に専用opaque UI overlay pipelineがある場合は、その条件を明示的にscene flagへし、一般opaque pipelineへ暗黙統合しない。

### 変更してはいけない範囲

- SoftwareRenderer3Dのpixel loopを呼ばない。
- CPU ownership maskを作らない。
- opaque drawをtexture単位でsortしない。
- fragment depthが必要なvariantを固定function depthへ置き換えない。
- NeedOpaqueを全translucent polygonへ常時実行しない。

### フェーズ完了時に残す状態

```text
opaque color/depth/attribute/stencilがGPU生成
DS polygon orderを保持
texture、toon、fog、depth variantがpipelineへ反映
NeedOpaqueが独立して機能
```

---

# 18. フェーズR13: translucent polygon pathを完成させる

## 18.1 blend

```text
src = SRC_ALPHA
dst = ONE_MINUS_SRC_ALPHA
color op = ADD
alpha op = MAX
```

## 18.2 depth

polygon attributeに従い次を分ける。

```text
depth less
depth equal
depth write enabled
depth write disabled
Z-buffer
W-buffer
```

## 18.3 translucent polygon ID

同じtranslucent polygon IDによる重複blendをstencilで防ぐ。

異なるIDのpolygonは重ね合わせを許可する。

## 18.4 background alpha zero

背景alpha zero時の専用pipelineを参照実装どおりに分離する。

一般translucent pipelineへ無理に統合しない。

---


## R13 AI Sonnetへの具体的な実装指示

### 目的

translucent polygonのblend、depth、polygon ID重複抑止、background alpha zero pathをGPUで実装する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
graphics raster fragment shader variants
depth/stencil state作成コード
```

### pipeline variant

次を含める。

```text
Z/W buffer
depth less/equal
depth write enabled/disabled
fog write enabled/disabled
alpha blend enabled
background alpha zero
texture mode
toon/highlight
```

### blend state

参照実装のcolor/alpha blend式をそのまま使う。

```text
src color factor = SRC_ALPHA
dst color factor = ONE_MINUS_SRC_ALPHA
color op = ADD
alpha op = MAX
```

必要なalpha factorは参照shader/output formatへ合わせる。

### translucent polygon ID

1. stencil lower bitsへpolygon IDを保持する。
2. 同じtranslucent polygon IDが同じpixelを再度blendしない条件を設定する。
3. 異なるIDの重ね合わせを許可する。
4. shadow用bit 7とpolygon ID bitsを衝突させない。
5. pass境界で必要なbitだけclearする。

### depth

polygon attributeから次を選ぶ。

```text
less
equal tolerance
write/no write
Z/W
```

W-buffer値をZ-buffer式へ誤変換しない。

### background alpha zero

参照実装の専用pipelineを維持する。

背景alpha zero pixelで通常translucentと異なるcolor/attribute処理が必要な場合は、明示variantへする。

### draw order

accelerated sceneのalpha draw range順を保持する。

NeedOpaque完了後にtranslucent fragmentを描画する。

### 変更してはいけない範囲

- CPUでpolygon ID maskを作らない。
- alpha polygonを一括sortしない。
- depth writeを全alpha polygonで固定しない。
- shadow bitとtranslucent IDを同じmaskとしてclearしない。
- background alpha zero専用処理を削除しない。

### フェーズ完了時に残す状態

```text
translucent blendがGPU
同一polygon IDの重複blend抑止
depth modeとwrite modeをpolygon単位で反映
background alpha zero pathが独立
```

---

# 19. フェーズR14: shadow pathを完成させる

shadowを次の段階へ分ける。

```text
Shadow Mask
Shadow Self Reject
Shadow Blend
```

## 19.1 Shadow Mask

depth fail時にstencil bit 7を設定する。

lower polygon ID bitsを保持する。

## 19.2 Shadow Self Reject

shadow polygon自身のpoly IDと一致するpixelのshadow bitを消す。

## 19.3 Shadow Blend

shadow bitが残るpixelだけblendする。

blend後にlower stencil bitsを参照実装どおり更新する。

## 19.4 stencil clear

必要なpass境界でshadow bitだけをclearするpipelineを使う。

depth/stencil image全体を不要にclearしない。

---


## R14 AI Sonnetへの具体的な実装指示

### 目的

DS shadow polygonをmask、self reject、blendの三段階としてVulkan depth/stencil上へ実装する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
shadow関連graphics shader
stencil pipeline state作成コード
```

### pass構成

```text
1. Shadow Mask
2. Shadow Self Reject / Clear
3. Shadow Blend
```

各passを明示的なpipelineとdraw listへする。

### Shadow Mask

1. shadow mask polygonをscene flagから抽出する。
2. depth fail条件でstencil bit 7を設定する。
3. lower polygon ID bitsを保持する。
4. color writeを無効化する。
5. depth writeを無効化する。
6. W/Z depth modeをpolygonに合わせる。

### Shadow Self Reject

1. shadow polygon自身のIDを取得する。
2. lower bitsが自身のIDに一致するpixelではbit 7をclearする。
3. 他のpolygon上のmask bitは保持する。
4. color/depthを変更しない。

### Shadow Blend

1. bit 7が設定されたpixelだけ通す。
2. shadow polygon color/alphaでblendする。
3. depth条件を参照実装へ合わせる。
4. blend後のstencil更新を参照実装へ合わせる。
5. background alpha zero専用variantを必要に応じて分離する。

### bit管理

```cpp
constexpr u8 kShadowMaskBit = 0x80;
constexpr u8 kTranslucentPolygonIdMask = 0x3F;
```

実際の参照bit幅へ合わせて定数化する。

### draw list

```text
GraphicsShadowMaskDrawIndices
GraphicsShadowDrawIndices
```

をaccelerated scene build結果から作る。

source orderを保持する。

### 変更してはいけない範囲

- shadowを通常translucent polygonとして一回だけ描かない。
- shadow maskをCPU bitmapへ作らない。
- stencil全体をpassごとにclearしない。
- polygon ID lower bitsをbit 7 clear時に破壊しない。
- depth fail semanticsをdepth passへ変更しない。

### フェーズ完了時に残す状態

```text
shadow maskがstencil bit
self shadowが除外
shadow blendが対象pixelだけへ適用
translucent IDとshadow bitが共存
```

---

# 20. フェーズR15: toon、highlight、edge、fog、AAを完成させる

## 20.1 toon table

32 entryをGPU bufferへuploadする。

opaque、translucent、textured、untexturedで共通利用する。

## 20.2 highlight

highlight modeを専用shader variantへする。

toonと同じbranchへ無理に統合しない。

## 20.3 edge marking

attribute imageのpolygon IDと隣接pixelを比較する。

edge tableを参照してfinal colorへ適用する。

## 20.4 fog

```text
RenderFogColor
RenderFogOffset
RenderFogShift
RenderFogDensityTable
```

をfinal passへ渡す。

fog write flagがあるpixelだけ処理する。

## 20.5 antialiasing

coverage情報をfinal passへ渡す。

edge、fogとの組み合わせをpipeline variantまたはspecializationへする。

---


## R15 AI Sonnetへの具体的な実装指示

### 目的

toon、highlight、edge marking、fog、antialiasingを3D final GPU passへ統合する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
graphics final vertex shader
graphics edge fragment shader
graphics edge-fog fragment shader
graphics fog fragment shader
関連shader ABI header
```

### toon table

1. 32 entryのtoon tableをper-context GPU bufferへuploadする。
2. dirty generationが変化した場合だけ更新する。
3. fragment shaderで5-bit intensityからentryを選ぶ。
4. toonとhighlightをmode bitで分ける。
5. textured/untextured双方で同じtableを使う。

### highlight

highlight color加算とclampを参照shader順序へ合わせる。

toon color置換とhighlight加算を同じ演算として扱わない。

### edge marking

1. AttrImageからcenter polygon IDを読む。
2. 隣接pixelのpolygon ID、depth、coverageを読む。
3. edge tableから色を選ぶ。
4. hidden alpha zero polygonのoverride条件を参照実装へ合わせる。
5. scaleに応じた隣接sample offsetを使う。
6. output colorへedgeを適用する。

### fog

1. fog density tableをGPU bufferまたはpush constantへpackする。
2. fog offset、shift、colorを渡す。
3. attributeのfog flagがあるpixelだけ処理する。
4. edgeとfogが同時有効なvariantを用意する。
5. alpha fogの有無を`RenderDispCnt`から反映する。

### antialiasing

1. coverage値をraster passからfinal passへ保持する。
2. coverageに基づいてedge pixelを背景と合成する。
3. edge marking、fogとの適用順を参照実装へ合わせる。
4. coverage fixの設定とDS antialiasingを混同しない。

### final pass選択

```text
none
edge
fog
edge + fog
AA combinations
```

を明示variantへする。

### 変更してはいけない範囲

- toon tableを毎draw uploadしない。
- edgeをCPU画像処理で追加しない。
- fogをpresenterへ移さない。
- high-resolution edge offsetをnative 1 pixel固定にしない。
- coverage fixを最終AA coverageとして代用しない。

### フェーズ完了時に残す状態

```text
toon/highlightがpolygon shader
edge/fog/AAがGPU final pass
attribute/depth/coverageを直接利用
CPU postprocessなし
```

---

# 21. フェーズR16: display captureを接続する

## 21.1 capture source

3D capture sourceはVulkan 3D targetと同じframe identityを使う。

## 21.2 capture line export

参照shaderを使用する。

```text
scaled 3D target
    ↓
native 256 pixel line
    ↓
DS color format
    ↓
capture line buffer
```

## 21.3 double buffer

capture line bufferは2 slot以上を持つ。

```text
active
pending
ready
```

を分離する。

## 21.4 `GetLine()`

通常2D合成のために毎line readbackしない。

`GetLine()`はcaptureまたはCPU同期が必要な場面だけexact line cacheを返す。

## 21.5 capture VRAM同期

GPU capture結果をVRAMへ反映する必要がある場合だけ、必要範囲をreadbackする。

normal presentationをcapture readbackへ依存させない。

---


## R16 AI Sonnetへの具体的な実装指示

### 目的

display captureがVulkan 3D frameを正しいnative DS lineへ変換し、必要なVRAM同期だけを行うよう接続する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU.h
src/GPU.cpp
src/GPU_Soft.cpp
capture line export shader
```

### capture state

frameごとに次を固定する。

```text
capture enabled
capture control
capture screen swap
3D frame serial
source image dimensions
scale
destination VRAM range
```

live registerを非同期command recording中に再読込しない。

### capture line export

1. 3D ColorImageをsampleする。
2. scaleされたpixelからnative 256×192 lineへ解決する。
3. DS 6-bitまたはcapture formatへ変換する。
4. screen swap hintを反映する。
5. capture line bufferへ書く。
6. shader writeからhost readへのbarrierを入れる。
7.必要lineだけexportする。

### buffer slots

最低2 slotを持つ。

```text
ActiveCaptureLineBufferSlot
PendingCaptureLineBufferSlot
ReadyCaptureLineBufferSlot
```

slotごとにcompletion stateを持つ。

### `GetLine()`

通常2D表示では呼ばれない前提にする。

capture処理がready slotを必要としたときだけ、完了済みbufferをline cacheへcopyする。

未完了frameを読むためにdevice全体を待たない。

### fallback

参照実装のexact capture履歴を保持する。

source frameが利用できない場合にSoftware 3Dを起動しない。

直前の有効captureまたは明示clear colorを使う。

### VRAM同期

1. destination bank/blockを特定する。
2. capture対象rangeだけCPUへ同期する。
3. GPU resultをDS VRAM formatへ書く。
4. VRAM dirty/capture flagsを更新する。
5. normal presentation用readbackと共有しない。

### 変更してはいけない範囲

- 毎frame3D target全体をreadbackしない。
- captureのためにSoftwareRenderer3Dを並走させない。
- readyでないslotを上書きしない。
- capture screen swapをpresent時のlive値から推測しない。
- capture line cacheを通常window outputに使わない。

### フェーズ完了時に残す状態

```text
display captureがVulkan 3D source
native line exportがGPU
必要rangeだけCPU/VRAM同期
normal presentationはreadback不要
```

---

# 22. フェーズR17: structured 2D producerを独立させる

## 22.1 `SoftRenderer`から分離する

現在の`SoftRenderer`内に追加された次を専用producerへ移す。

```text
StructuredPlane0
StructuredPlane1
StructuredControl
StructuredNativeFinal
StructuredFrameSerial
```

## 22.2 class

```cpp
class VulkanStructured2DProducer
{
public:
    void BeginFrame(
        u64 frameSerial,
        bool screenSwap);

    void SubmitEngineLine(
        u32 engine,
        u32 line,
        const u32* plane0,
        const u32* plane1,
        const u32* control,
        const u32* nativeFinal,
        u32 dispCnt,
        u16 masterBrightness,
        bool enabled,
        bool forcedBlank,
        bool screensEnabled);

    void EndFrame();
    const VulkanStructured2DFrame& PublishedFrame() const;
};
```

## 22.3 2D renderer連携

`SoftRenderer2D`のlayer evaluationは再利用してよい。

ただし外側`SoftRenderer`のCPU framebuffer完成処理を必要条件にしない。

Engine A、Engine Bごとに次を直接producerへ渡す。

```text
plane 0
plane 1
control
line metadata
display mode
master brightness
```

## 22.4 native final

VRAM display、FIFO display、forced blankなどstructured layer pairで表現できないmodeだけnative finalを使用する。

regular displayの通常pixelをnative finalへ丸ごと依存させない。

## 22.5 frame identity

structured 2D frameへ次を持たせる。

```text
FrameSerial
FrontBuffer
ScreenSwap
Generation
Complete
```

---


## R17 AI Sonnetへの具体的な実装指示

### 目的

2D layer evaluationを再利用しながら、CPU final framebufferを作らずにVulkan compositor用structured metadataを生成する専用producerを実装する。

### 対象ファイル

```text
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
src/GPU_Soft.h
src/GPU_Soft.cpp
src/GPU2D_Vulkan.h
src/GPU2D_Vulkan.cpp
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU3D.h
```

### producer class

```cpp
class VulkanStructured2DProducer
{
public:
    void Reset();
    void BeginFrame(
        u64 frameSerial,
        u64 generation,
        int frontBuffer,
        bool screenSwap);

    void SubmitEngineLine(
        const SapphireStructured2DLine& line);

    bool EndFrame();
    const VulkanStructured2DFrame& GetPublishedFrame() const;
};
```

### frame storage

`VulkanStructured2DFrame`へ次を持たせる。

```text
Plane0 top/bottom
Plane1 top/bottom
Control top/bottom
NativeFinal top/bottom
LineMeta top/bottom
LineState top/bottom
FrameSerial
Generation
FrontBuffer
ScreenSwap
Complete
```

### `SoftRenderer2D`の再利用

1. layer priority、BG、OBJ、window、color effect計算を再利用する。
2. 最終screen BGRA bufferへのcopyは行わない。
3. plane0、plane1、controlをline callbackへ渡す。
4. Vulkan callbackはbackend-neutral interface越しに呼ぶ。
5. Software renderer使用時は既存final framebuffer経路を保持する。
6. MelonPrime Vulkan時だけstructured callbackを有効化する。

### control ABI

次のbitを共通headerへ定義する。

```text
effect mask
EVA shift
EVB shift
EVY shift
direct 3D bit
valid bit
composition class
layer ownership
```

shader側と同じ値を使う。

### native final

次のmodeだけnative finalを作る。

```text
screen off
forced blank
VRAM display
FIFO display
structured pairで表現不能なmode
```

regular displayの全pixelをnative finalへcopyしない。

### frame completion

1. line 0で`BeginFrame()`する。
2. Engine A/Bの各lineを一度ずつ受ける。
3. 受信maskで384 engine-lineの完了を管理する。
4. line 191だけで無条件completeにしない。
5. frame serial/generation不一致packetを破棄する。
6. complete frameだけpublishする。

### `SoftRenderer`から削除

```text
StructuredPlane0
StructuredPlane1
StructuredControl
StructuredNativeFinal
StructuredFrameSerial
VulkanRenderer3DへのBegin/Submit/End呼出し
```

### 変更してはいけない範囲

- producerを`VulkanRenderer3D`へ戻さない。
- regular displayをCPU final imageとして完成させない。
- line callbackからVulkan submitを直接行わない。
- live GPU state pointerをpublished frameへ保持しない。
- incomplete frameを前frameと混ぜない。

### フェーズ完了時に残す状態

```text
2D metadata producerが独立
SoftRenderer outer ownership不要
regular displayはplane/controlで表現
frame identityとcomplete stateが固定
```

---

# 23. フェーズR18: `VulkanOutput`へfinal compositionを集約する

## 23.1 `VulkanRenderer3D`内の重複compositorを削除する

A3～A7で追加されたcomposition resourceを`VulkanOutput`へ統合する。

compositor shader module、descriptor、pipeline、command bufferを一か所だけ所有する。

## 23.2 input

```cpp
struct VulkanCompositionInputs
{
    VkImage sourceImage;
    VkImageView sourceImageView;

    VkImage previousTopSourceImage;
    VkImageView previousTopSourceImageView;

    VkImage previousBottomSourceImage;
    VkImageView previousBottomSourceImageView;

    VkBuffer topPackedBuffer;
    VkBuffer bottomPackedBuffer;
    VkBuffer capture3dBuffer;

    u32 scale;
    u32 rendererWidth;
    u32 rendererHeight;
    u32 packedStride;
    u32 screenSwap;

    bool previousTopSourceValid;
    bool previousBottomSourceValid;
    bool capture3dSourceValid;
    bool liveSourceScreenSwap;
};
```

参照実装の入力fieldを維持する。

## 23.3 output image

frame slotごとに2-screen output imageを持つ。

```text
width = 256 × scale
height = 192 × scale × 2
```

またはarray layer 2とする。

参照shaderとpresenterの期待する形式へ統一する。

## 23.4 descriptor

bindingを参照shader ABIへ完全一致させる。

現在の単一structured buffer bindingを、top/bottom packed bufferへ分離する。

## 23.5 composition dispatch

次を一つのcommand bufferへ記録する。

```text
packed 2D upload barrier
3D image barrier
history image barrier
capture buffer barrier
compositor bind
descriptor bind
push constants
vkCmdDispatch
output image barrier
timeline signal
```

## 23.6 previous source

topとbottomのprevious 3D sourceを別々に保持する。

screen swap、capture、class4表示で必要な履歴をframe identity付きで管理する。

---


## R18 AI Sonnetへの具体的な実装指示

### 目的

3D image、structured 2D metadata、capture/historyを`VulkanOutput`でGPU合成し、最終two-screen Vulkan imageを生成する。

### 対象ファイル

```text
src/frontend/qt_sdl/VulkanReference/VulkanOutput.h
src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp
src/frontend/qt_sdl/VulkanReference/VulkanCompositorShader.comp
src/frontend/qt_sdl/VulkanReference/VulkanAccumulate3dShader.comp
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU2D_Vulkan.h
src/GPU2D_Vulkan.cpp
```

### ownership

`VulkanOutput`だけが次を所有する。

```text
final output images
top packed buffer
bottom packed buffer
capture 3D buffer
previous top 3D image reference
previous bottom 3D image reference
compositor descriptor resources
compositor pipeline
composition command resources
temporal composition state
```

### packed buffer

参照frontendのlayoutへ揃える。

topとbottomを別bufferまたは別bindingとして扱う。

各lineへ次をpackする。

```text
plane0[256]
plane1[256]
control[256]
line metadata
```

現在の`plane0 + plane1 + control + nativeFinal + 2 words`独自strideを参照shader ABIへ直接渡さない。

必要なnative final/capture dataは参照input contractへ変換する。

### frame resource

Frame slotごとに次を作る。

```text
output image
output image view
device memory
composition command buffer
completion fence/semaphore
packed buffers
capture buffer
history references
```

resource寸法はscaleに応じる。

### descriptor

参照ABIのbindingを厳密に使う。

```text
0 OutputImage
1 Current3DImage
2 TopPackedBuffer
3 BottomPackedBuffer
4 PreviousTop3DImage
5 Capture3DBuffer
6 PreviousBottom3DImage
```

dummy resourceが必要なbindingへnull descriptorを渡さない。

### composition input構築

`VulkanRenderer`は次を`VulkanOutput`へ渡す。

```text
Vulkan3DFrameView
VulkanStructured2DFrame
capture state
front buffer
screen swap
scale
filtering
generation
```

`VulkanOutput`内で参照実装のownership/classificationを構築する。

### command recording

```text
host packed write flush
buffer barrier
3D image wait/layout barrier
history input barrier
output image general layout
pipeline bind
descriptor bind
push constants
dispatch
output image shader-read/present input layout
completion signal
```

### temporal history

1. topとbottomを独立して保持する。
2. frame/generation/screen swapを一緒に記録する。
3. capture-backed class4 stateを参照実装へ合わせる。
4. old generation historyを破棄する。
5. history参照中frameをFrameQueueへ返さない。

### `VulkanRenderer3D`との重複排除

A3～A7由来のcompositor resourceを一切参照しない。

`VulkanOutput`を唯一のfinal compositorとする。

### 変更してはいけない範囲

- 3D imageをCPUへ戻してから合成しない。
- top/bottom packed bufferを単一独自ABIへ再統合しない。
- final outputを`VulkanRenderer3D::ColorImage`へ上書きしない。
- previous sourceをraw pointerだけで保持しない。
- null descriptorを未定義のままbindしない。

### フェーズ完了時に残す状態

```text
final two-screen imageがGPU composition
descriptor ABIが参照shaderと一致
history/capture/screen swapがframe identity付き
compositor ownerがVulkanOutputだけ
```

---

# 24. フェーズR19: `FrameQueue`を正式な出力所有者にする

## 24.1 frame構造

desktop Vulkan用に次を持たせる。

```cpp
struct Frame
{
    FrameBackend backend;
    u32 width;
    u32 height;
    u64 frameId;

    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;

    VkFence renderFence;
    VkSemaphore renderTimeline;
    u64 renderTimelineValue;

    VkFence presentFence;
    VkSemaphore presentTimeline;
    u64 presentTimelineValue;
};
```

## 24.2 queue size

参照実装の9 frame policyを基準にする。

fast forward時のbacklogとnormal frameのlatency policyを分離する。

## 24.3 state

```text
free
rendering
ready to present
pending present
previous frame
retired
```

を明確にする。

## 24.4 frame reuse

presenterが参照中のframeをrendererへ戻さない。

previous sourceとして保持中のframeを上書きしない。

## 24.5 generation

renderer再作成、scale変更、ROM切替、savestate復帰でgenerationを更新する。

旧generation frameを新しいpresenterへ渡さない。

---


## R19 AI Sonnetへの具体的な実装指示

### 目的

renderer、compositor、presenter間のVulkan frame ownershipを`FrameQueue`へ統一する。

### 対象ファイル

```text
src/frontend/qt_sdl/VulkanReference/FrameQueue.h
src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.h
src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp
src/GPU.h
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
```

### Frame構造

desktop Vulkan frameへ次を持たせる。

```text
backend kind
frame ID
generation
width
height
VkImage
VkImageView
VkDeviceMemory
image layout
render completion semaphore/value
present completion semaphore/value
render fence
present fence
state
reference count
history reference flags
```

### state machine

```text
Free
Rendering
Ready
AcquiredForPresentation
PendingPresent
ReferencedAsHistory
Retired
```

state遷移を関数へ集約する。

fieldを任意の場所から直接変更しない。

### queue数

参照実装の9 frame policyを基本値にする。

constantとして一か所へ定義する。

### producer flow

```text
AcquireFreeFrame
EnsureFrameResources
MarkRendering
ComposeAndSubmit
MarkReady
PublishLatest
```

### consumer flow

```text
AcquireLatestReadyFrame
MarkAcquiredForPresentation
Present
MarkPendingPresent
Completion確認
ReleasePresentationReference
ReturnFreeまたはHistory保持
```

### history reference

previous top/bottom sourceとして参照中のframeへ別referenceを持たせる。

present完了だけでfreeへ戻さない。

### output lease

`RendererOutputLease`がframe referenceを保持する。

lease destructorでqueueへreleaseする。

move-onlyにする。

### generation

次でgenerationを更新する。

```text
renderer recreation
scale change
ROM replacement
savestate restore
device loss recovery
surface format incompatible change
```

旧generation frameはlatest outputとして返さない。

### fast forward

ready frameが過剰に溜まる場合は古い未取得frameをdropできるpolicyを実装する。

present中、history参照中、lease中のframeはdropしない。

### 変更してはいけない範囲

- raw `Frame*`を無期限に保持しない。
- presenterがqueue stateを直接書き換えない。
- rendererがpresent中imageへ再描画しない。
- history参照とpresentation参照を同じboolで管理しない。
- frame free時に未完了fenceを無視しない。

### フェーズ完了時に残す状態

```text
frame ownershipがstate machine
rendererとpresenterの同時利用なし
history referenceが独立
generation跨ぎ再利用なし
move-only output leaseがframeを保護
```

---

# 25. フェーズR20: desktop surface adapterを実装する

## 25.1 共通interface

```cpp
class VulkanDesktopSurface
{
public:
    virtual ~VulkanDesktopSurface() = default;

    virtual VkSurfaceKHR CreateSurface(
        VkInstance instance) = 0;

    virtual QSize PixelSize() const = 0;
    virtual void* NativeHandle() const = 0;
};
```

## 25.2 Windows

Qt windowの`winId()`から`HWND`を取得する。

```cpp
VkWin32SurfaceCreateInfoKHR
```

を使う。

`HINSTANCE`は`GetModuleHandleW(nullptr)`から取得する。

## 25.3 X11

Qt native interfaceからDisplayとWindowを取得する。

```cpp
VkXlibSurfaceCreateInfoKHR
```

またはXCB variantを使用する。

XlibとXCBを混在させない。

## 25.4 Wayland

Qt native interfaceから`wl_display`と`wl_surface`を取得する。

```cpp
VkWaylandSurfaceCreateInfoKHR
```

を使う。

surface再作成時に古い`wl_surface`を保持しない。

## 25.5 macOS

`CAMetalLayer`を持つnative viewからMetal surfaceを生成する。

```cpp
VkMetalSurfaceCreateInfoEXT
```

MoltenVK build gate内へ限定する。

---


## R20 AI Sonnetへの具体的な実装指示

### 目的

Qt native windowからplatform別`VkSurfaceKHR`を作るdesktop surface adapterを実装する。

### 対象ファイル

```text
src/VulkanDesktopSurface.h
src/VulkanDesktopSurface.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.h
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/MelonPrimeScreenVulkan.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/CMakeLists.txt
```

### 共通interface

```cpp
class VulkanDesktopSurface
{
public:
    virtual ~VulkanDesktopSurface() = default;
    virtual bool IsValid() const noexcept = 0;
    virtual VkSurfaceKHR CreateSurface(VkInstance instance) = 0;
    virtual QSize PixelSize() const = 0;
    virtual void* NativeHandle() const noexcept = 0;
    virtual VulkanWindowSystem WindowSystem() const noexcept = 0;
};
```

### Windows

1. `QWidget::winId()`から`HWND`を取得する。
2. native child windowが必要なら専用widgetを作る。
3. `GetModuleHandleW(nullptr)`で`HINSTANCE`を取得する。
4. `vkCreateWin32SurfaceKHR`を呼ぶ。
5. HWND再作成時に旧surfaceを破棄する。
6. logical sizeではなくdevice pixel sizeを返す。

### Linux X11/XCB

1. Qt platform native interfaceからconnection/displayとwindow IDを取得する。
2. XlibかXCBの一方へ統一する。
3. 対応するinstance extensionとcreate infoを使う。
4. Qt backendがWaylandのときX11 handleを要求しない。

### Wayland

1. `wl_display`と`wl_surface`をQt native interfaceから取得する。
2. `vkCreateWaylandSurfaceKHR`を使う。
3. surface configure前の0×0 sizeを扱う。
4. widget再parent、fullscreen切替でnative surface generationを更新する。

### macOS

1. native NSViewへ`CAMetalLayer`を設定する。
2. `VkMetalSurfaceCreateInfoEXT`を使う。
3. layerのdrawable sizeをdevice pixel ratio込みで更新する。
4. Objective-C++実装を`.mm`へ分離する。
5. MoltenVK build gate内だけでcompileする。

### surface capability API

作成したsurfaceに対して`VulkanContext`へ次を問い合わせる。

```text
present queue family
surface formats
present modes
min/max image count
current/min/max extent
supported transforms
composite alpha
```

### lifetime

surface ownerは`ScreenPanelVulkan`またはsurface hostだけにする。

presenterはborrowed handleとして受け取り、detach時に使用を停止する。

### 変更してはいけない範囲

- `VulkanRenderer3D`からQt native handleを読む。
- renderer threadから`QWidget` APIを呼ぶ。
- X11とWayland handleを推測でcastする。
- macOSでWin32 surface extensionを要求する。
- old native window generationのsurfaceを再利用する。

### フェーズ完了時に残す状態

```text
各desktop platformでVkSurfaceKHR生成可能
surfaceとnative window generationが連動
device pixel sizeを取得
renderer coreはQt/native handleを知らない
```

---

# 26. フェーズR21: `VulkanSurfacePresenter`をQtへ接続する

## 26.1 `ScreenPanelVulkan`のstubを削除する

次を廃止する。

```cpp
void ScreenPanelVulkan::drawScreen()
{
    ScreenPanelNative::drawScreen();
}
```

## 26.2 `ScreenPanelVulkan`所有物

```cpp
std::unique_ptr<VulkanDesktopSurface> SurfaceHost;
std::unique_ptr<VulkanSurfacePresenter> Presenter;
std::unique_ptr<VulkanOutputBridge> OutputBridge;
int SurfaceId;
```

## 26.3 init

```text
native child window作成
VulkanContext Acquire
surface作成
presenter init
attachSurface
layout config生成
background config生成
```

## 26.4 draw

```text
rendererのlatest Frame取得
composition inputs取得
surface config更新
presentFrame
present完了後frameをqueueへ返却
```

CPU framebufferを取得しない。

## 26.5 resize

resize eventでは次だけを行う。

```text
pixel size更新
swapchain dirty
surface config更新
```

3D rendererとtexture cacheを再作成しない。

## 26.6 layout

既存`ScreenLayout`から次を`VulkanSurfaceConfig`へ変換する。

```text
top rect
bottom rect
hybrid top rect
hybrid bottom rect
alpha
draw order
background mode
filter mode
```

## 26.7 HUD、OSD、radar

最終outputをCPUへ戻さない。

次のどちらかでGPU合成する。

```text
presenter内overlay pass
compositor outputへの追加layer
```

radarはbottom screen textureを直接sampleする。

---


## R21 AI Sonnetへの具体的な実装指示

### 目的

`ScreenPanelVulkan`を実際のVulkan swapchain presenterへ接続し、Native CPU presenterへの委譲を完全に除去する。

### 対象ファイル

```text
src/frontend/qt_sdl/MelonPrimeScreenVulkan.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
src/frontend/qt_sdl/Screen.h
src/frontend/qt_sdl/Screen.cpp
src/frontend/qt_sdl/MainWindow.cpp
src/frontend/qt_sdl/EmuInstance.cpp
```

### 削除するstub

```cpp
void ScreenPanelVulkan::drawScreen()
{
    ScreenPanelNative::drawScreen();
}
```

`initVulkan()`の「CPU safety presentation active」ログと常時成功実装を削除する。

### `ScreenPanelVulkan` fields

```text
VulkanDesktopSurface owner
VulkanSurfacePresenter owner
surface ID
surface generation
current VulkanSurfaceConfig
last configured pixel size
renderer output generation
pending repaint flag
device loss state
```

### 初期化

```text
native surface host作成
VulkanContext Acquire
VkSurfaceKHR作成
VulkanSurfacePresenter init
attachSurface
initial swapchain作成
layout config作成
surface config適用
```

いずれかが失敗した場合は途中resourceを逆順で解放する。

### `drawScreen()`

1. active NDS/GPUから`RendererOutputLease`を取得する。
2. output kindがVulkanか確認する。
3. leaseから`Frame`とcomposition inputを取得する。
4. current layoutを`VulkanSurfaceConfig`へ変換する。
5. resize/config変更がある場合だけpresenterへ反映する。
6. `presentFrame()`を呼ぶ。
7. present submission後にleaseを適切なstateへ移す。
8. CPU framebuffer、`QImage`、`QPainter`を使用しない。

### swapchain

1. surface formatを参照presenter policyで選ぶ。
2. present modeをVSync設定へ対応させる。
3. extent 0×0ではpresentを保留する。
4. resize、out-of-date、suboptimalでswapchainを再作成する。
5. old swapchainをnew swapchain create infoへ渡す。
6. old swapchain imageのpresent完了後に破棄する。

### layout

既存`ScreenLayout`から次を変換する。

```text
top rect
bottom rect
hybrid rect
draw order
alpha
background mode
filter mode
```

device pixel ratioを反映する。

### radar、HUD、OSD

GPU overlay passとして実装する。

radarでbottom screenをtopへ表示する場合は、final frame imageのbottom regionをsampleする。

CPU image copyを作らない。

overlay resourceはpresenter側に置き、3D rendererへ入れない。

### pause画面

最後にpresentされたVulkan frameをFrameQueue reference付きで保持する。

pause中にNative CPU presenterへ切り替えない。

### 変更してはいけない範囲

- `ScreenPanelNative::drawScreen()`を呼ばない。
- Vulkan outputを`QImage`へ変換しない。
- GUI threadからlive VRAM/GPU registerを読む。
- presenterが`VulkanRenderer3D`のprivate resourceを直接所有する。
- surface resizeで3D texture cacheを破棄する。

### フェーズ完了時に残す状態

```text
Vulkan frameがswapchainへ直接present
Qt CPU painter経路なし
layout、radar、HUDがGPU overlay
pause中もVulkan frameを保持
```

---

# 27. フェーズR22: renderer factoryとEmuThreadを一本化する

## 27.1 factory

```cpp
case renderer3D_Vulkan:
    return std::make_unique<VulkanRenderer>(
        nds,
        VulkanRendererMode::Graphics);
```

`SoftRenderer`を返さない。

## 27.2 override factoryを削除する

```text
CreateRenderer3DOverrideForSelection
```

を削除する。

## 27.3 EmuThread

`updateRenderer()`は次だけを行う。

```text
requested renderer正規化
presentation backend決定
新Renderer生成
GPU::SetRenderer
RendererSettings適用
screen panel backend切替通知
```

GPU3D overrideの作成、設定、破棄を行わない。

## 27.4 presentation backend

```text
Software → NativeQt
OpenGL → OpenGL
Metal → Metal
Vulkan → Vulkan
```

Vulkan rendererなのにNativeQt CPU presenterを選ばない。

## 27.5 Vulkan Compute設定migration

既存設定値がVulkan Computeの場合はVulkan Graphicsへ正規化する。

設定保存時も正式なVulkan IDへ書き戻す。

---


## R22 AI Sonnetへの具体的な実装指示

### 目的

renderer factory、presentation backend選択、EmuThreadのrenderer更新を一つのtransactionへ統合する。

### 対象ファイル

```text
src/frontend/qt_sdl/MelonPrimeRendererFactory.h
src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp
src/frontend/qt_sdl/MelonPrimeVideoBackend.h
src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp
src/frontend/qt_sdl/EmuThread.h
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/EmuInstance.cpp
src/frontend/qt_sdl/MainWindow.cpp
```

### factory API

`CreateRendererForSelection()`だけを正式生成APIにする。

戻り値:

```cpp
struct RendererCreationResult
{
    std::unique_ptr<melonDS::Renderer> Renderer;
    PresentationBackend Presentation;
    int RequestedRenderer = renderer3D_Software;
    int NormalizedRenderer = renderer3D_Software;
    int ActualRenderer = renderer3D_Software;
    std::string FailedStage;
    std::string FallbackReason;
};
```

### renderer mapping

```text
Software → SoftRenderer + NativeQt
OpenGL → GLRenderer + OpenGL
OpenGL Compute → GLRenderer(compute) + OpenGL
Metal → MetalRenderer + Metal
Metal Compute → MetalRenderer(compute) + Metal
Vulkan → VulkanRenderer(Graphics) + Vulkan
```

### Vulkan Compute migration

既存`renderer3D_VulkanCompute`設定はVulkan Graphicsへ正規化する。

UI、config load、config saveの全箇所で同じmigration関数を使う。

Vulkan Computeというactual backend名を表示しない。

### `EmuThread::updateRenderer()`

次の順へする。

```text
config snapshot取得
requested renderer正規化
factoryでRendererCreationResult作成
render lock取得
旧presentation停止
GPU::SetRenderer(new renderer)
RendererSettings適用
presentation panel切替
videoRenderer/videoBackend更新
render lock解放
UIへactual backend通知
```

factoryと`GPU::SetRenderer()`の間で別のRenderer3Dを作らない。

### OpenGL context

`useOpenGL`はpresentation contextの有無だけを表す。

Vulkan/Metal renderer identityを`useOpenGL == false`だからSoftwareへclampしない。

### fallback

正式VulkanRendererの`Init()`が失敗した場合だけSoftware rendererを新規生成する。

同じ`RendererCreationResult`へ実際のfailed stageを記録する。

### screen panel切替

rendererとpresentation backendを同一transactionで切り替える。

VulkanRendererがactiveなのに`ScreenPanelNative`がactiveになる中間状態を公開しない。

### 変更してはいけない範囲

- override factoryを復活させない。
- Vulkan選択でSoftRendererを正常系として返さない。
- `useOpenGL`でVulkan renderer IDを上書きしない。
- renderer更新後に古いpanelが旧frame pointerを保持しない。
- config値とactual rendererを同じ変数だけで表現しない。

### フェーズ完了時に残す状態

```text
renderer生成APIが一つ
renderer identityとpresentation backendが同期
Vulkan Compute設定はVulkanへmigration
fallback理由が実際の初期化stage
```

---

# 28. フェーズR23: frame lifecycleを参照実装へ揃える

## 28.1 frame開始

```text
Renderer::Start3DRendering
VulkanRenderer3D::RenderFrame
```

## 28.2 VCount 144

参照実装のearly submit/capture処理を呼ぶ。

```text
Renderer::VCount1443D
VulkanRenderer3D::VCount144
```

## 28.3 scanline 0～191

```text
2D metadata生成
capture必要時だけ3D line取得
```

通常2D合成のために`GetLine()`を毎line呼ばない。

## 28.4 frame完了

```text
VulkanRenderer3D::FinishRendering
structured 2D frame publish
VulkanOutput::prepareFrameForPresentation
VulkanOutput::composeAndSubmitFrame
FrameQueue::pushRenderedFrame
```

## 28.5 VBlank

```text
frame identity確定
history更新
presenterへframe ready通知
```

---


## R23 AI Sonnetへの具体的な実装指示

### 目的

GPU scanline/frame eventとVulkan renderer、2D producer、VulkanOutputの呼出し順を参照実装へ揃える。

### 対象ファイル

```text
src/GPU.h
src/GPU.cpp
src/GPU3D.h
src/GPU3D.cpp
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

### frame state

`VulkanRenderer`へ明示的なframe stateを持たせる。

```cpp
struct VulkanFrameLifecycleState
{
    u64 FrameSerial = 0;
    u64 Generation = 0;
    bool ThreeDStarted = false;
    bool VCount144Submitted = false;
    bool Structured2DStarted = false;
    bool Structured2DComplete = false;
    bool CompositionSubmitted = false;
    bool Published = false;
};
```

### 呼出し順

#### frame開始

```text
GPU frame event
VulkanRenderer::BeginFrame
VulkanRenderer3D::BeginCaptureFrame
VulkanRenderer3D::RenderFrame
VulkanStructured2DProducer::BeginFrame
```

#### VCount 144

```text
Renderer::VCount1443D
VulkanRenderer3D::VCount144
early submit/capture preparation
```

#### active scanline

```text
Engine A/B layer evaluation
structured line submit
capture必要時だけ3D native line取得
```

#### frame完了

```text
VulkanRenderer3D::FinishRendering
structured 2D EndFrame
VulkanOutput prepare
VulkanOutput compose submit
FrameQueue publish
```

#### VBlank end/restart

```text
temporal history latch
capture state latch
VulkanRenderer3D::RestartFrame
next frame state reset
```

### frame serial

3D、2D、composition、FrameQueueの全てで同じserial/generationを使う。

各subsystemが独自にserialを加算しない。

### capture screen swap

frame開始時または参照実装指定時点でlatchし、frame途中のlive値を使わない。

### `GetLine()`

2D producer通常経路から外す。

capture pathだけが必要なlineを要求する。

### pause、frame step

frame stepでも通常と同じlifecycleを一回実行する。

pauseだけではin-flight frameを破棄しない。

### fast forward

frame drop policyはFrameQueueで行う。

3D lifecycleの途中を省略しない。

### 変更してはいけない範囲

- subsystemごとに異なるframe serialを使わない。
- line 191到達前にincomplete 2D frameをpublishしない。
- VBlank live stateをGUI threadへ渡さない。
- fast forward時にcapture/history更新だけ省略しない。
- `RenderFrame()` wrapper内で複数lifecycle stageを暗黙に呼ばない。

### フェーズ完了時に残す状態

```text
3D、2D、composition、presentationが同一frame identity
呼出し順が明示
通常2DでGetLine不使用
pause/frame step/fast forwardでもownershipが維持
```

---

# 29. フェーズR24: 同期とresource lifetimeを整理する

## 29.1 fence

render context再利用に使う。

## 29.2 timeline semaphore

次のproducer valueを持つ。

```text
3D complete
composition complete
present complete
```

## 29.3 barrier

最低限次を明示する。

```text
host write → vertex shader read
host write → compute shader read
color attachment write → sampled image read
storage image write → sampled image read
shader write → indirect command read
transfer write → shader read
composition write → presenter fragment read
```

## 29.4 queue family ownership

graphics queueとpresent queueが異なる場合はownership transferを行う。

同じqueue familyでは`VK_QUEUE_FAMILY_IGNORED`を使う。

## 29.5 destroy順

```text
new frame publish停止
presenter frame取得停止
present中frame完了
FrameQueue clear
VulkanOutput shutdown
VulkanRenderer3D Stop
texture cache破棄
surface presenter shutdown
surface破棄
VulkanContext Release
```

---


## R24 AI Sonnetへの具体的な実装指示

### 目的

3D render、composition、presentの同期とresource破棄順を明示し、race、早期再利用、全体待機を除去する。

### 対象ファイル

```text
src/VulkanContext.h
src/VulkanContext.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.*
src/frontend/qt_sdl/VulkanReference/FrameQueue.*
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*
```

### timeline model

利用可能な場合は次の単調増加valueを使う。

```text
3D complete value
composition complete value
present complete value
```

同じsemaphoreを使う場合もstageごとのvalueをframeへ保存する。

timeline非対応時はbinary semaphore/fence adapterを使用する。

### barrier helper

用途別の明示関数を実装する。

```text
HostWriteToShaderRead
TransferWriteToShaderRead
ColorAttachmentToSampled
StorageWriteToSampled
ShaderWriteToIndirectRead
CompositionWriteToPresenterRead
PresentToRender
RenderToPresent
```

genericな`ALL_COMMANDS + MEMORY_READ/WRITE`だけに統一しない。

### queue submit

1. command recording後に必要wait semaphoreを組み立てる。
2. queue lockを取得する。
3. submitする。
4. queue lockを解放する。
5. CPU側はfenceを必要なresource再利用時だけ待つ。

### queue family transfer

graphics familyとpresent familyが異なる場合はoutput imageへrelease/acquire barrierを入れる。

同一familyではownership transferを行わない。

### resource retire queue

次のresourceを即時破棄せずretireする。

```text
old render targets
old swapchain
old frame images
old texture images
old descriptor pools
old pipelines
```

各entryへcompletion semaphore/valueまたはfenceを関連付ける。

### device loss

device loss stateをcontextへ通知する。

新規submitを停止し、frame publishを無効化し、resource ownerを上位から順にshutdownする。

CPU presenterへ自動切替して壊れたVulkan frameを表示しない。

### destroy順

実装を次へ揃える。

```text
output acquisition停止
presenter detach
pending present completion
FrameQueue release
VulkanOutput shutdown
VulkanRenderer3D shutdown
texture cache shutdown
surface destroy
VulkanContext Release
```

### 変更してはいけない範囲

- normal frameで`vkDeviceWaitIdle()`しない。
- barrierのoldLayoutを常にUNDEFINEDにしない。
- present queue family transferを無視しない。
- old swapchainを即時破棄しない。
- device loss後に既存queueへsubmitしない。

### フェーズ完了時に残す状態

```text
3D→composition→presentの依存がsemaphoreで明示
必要resourceだけfence待機
queue family ownershipを処理
retire queueで安全に破棄
```

---

# 30. フェーズR25: pipeline cacheを整理する

## 30.1 key

```text
vendor ID
device ID
driver version
pipeline cache UUID
reference port version
shader manifest hash
descriptor indexing mode
```

## 30.2 owner

3D pipeline cacheは`VulkanRenderer3D`が所有する。

compositor/presenter pipeline cacheはfrontend側が所有する。

同じcache fileへ異なるpipeline ABIを書かない。

## 30.3 lazy creation

基本pipelineを初期化時に作る。

大量variantは最初の利用時に作る。

---


## R25 AI Sonnetへの具体的な実装指示

### 目的

3D、compositor、presenterのpipeline cacheをABIごとに分離し、deviceとshader manifestへ対応した永続cacheとして整理する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.*
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*
src/VulkanReferencePortVersion.h
shader manifest/ABI header
Platform file API
```

### cache key

最低限次を含める。

```text
vendor ID
device ID
driver version
pipeline cache UUID
Sapphire core commit
Sapphire frontend tag
shader manifest hash
descriptor indexing mode
texture sampling path
pipeline ABI version
```

### cache分離

```text
3D graphics/compute cache
VulkanOutput compositor cache
VulkanSurfacePresenter cache
```

を別fileへする。

異なるpipeline layoutを同じfileへ保存しない。

### load

1. file headerを読む。
2. magic、version、keyを確認する。
3. key不一致ならdataを利用しない。
4. `VkPipelineCacheCreateInfo`へinitial dataを渡す。
5. invalid dataで`vkCreatePipelineCache`が失敗した場合は空cacheを作る。

### save

1. owner shutdown前にcache data sizeを取得する。
2. dataを取得する。
3. temporary fileへ書く。
4. atomic renameする。
5. dataが空の場合は既存有効cacheを上書きしない。
6. 複数instanceが同じfileへ同時書込しないようlockする。

### lazy pipeline creation

初期frameに必須なpipelineだけ先に作る。

variant pipelineは最初の利用時に作り、同じkeyで再利用する。

pipeline creation中にin-flight frame resourceを変更しない。

### cache invalidation

次でkeyを変更する。

```text
shader source変更
descriptor ABI変更
push constant変更
pipeline state key変更
reference version変更
```

### 変更してはいけない範囲

- deviceのpipeline cache UUIDを無視しない。
- 3Dとpresenterのcache dataを結合しない。
- cache load失敗をrenderer全体の失敗にしない。
- 毎framecacheを保存しない。
- shader ABI変更後も同じversion keyを使わない。

### フェーズ完了時に残す状態

```text
owner別pipeline cache
device/driver/shader ABI一致時だけ再利用
variantはlazy creation
cache破損がrender resourceを破壊しない
```

---

# 31. フェーズR26: current A1～A7重複実装を削除する

## 31.1 `GPU_Vulkan.*`

削除:

```text
VulkanRendererShellContract
DescribeVulkanRendererShell
CreateSapphireVulkanRenderer3D
```

置換:

```text
VulkanRenderer
VulkanRuntimeCapabilities
```

## 31.2 `GPU3D.*`

削除:

```text
CurrentRenderer
SetCurrentRenderer
GetCurrentRendererOverride
```

## 31.3 `Renderer`

削除:

```text
GPU3D override lookup
```

## 31.4 `SoftRenderer`

削除:

```text
Vulkan structured metadata frame owner
Vulkan BeginStructured2DFrame呼出し
Vulkan SubmitStructured2DLine呼出し
Vulkan EndStructured2DFrame呼出し
```

structured 2D producerへ移動する。

## 31.5 `GPU3D_Vulkan.*`

削除:

```text
Sapphire composition structs
Sapphire composition image
Sapphire composition descriptor
Sapphire composition command context
Sapphire composition shader module
structured 2D frame storage
structured 2D GPU upload storage
```

## 31.6 `MelonPrimeScreenVulkan.*`

stub実装を削除する。

Native CPU presenterへの委譲を削除する。

## 31.7 backup directory

build、source discovery、code searchへ影響するbackup source treeをrepository外へ移す。

少なくとも次を通常source treeとして扱わない。

```text
.melonprime_sapphire_vulkan_*_backup
.melonprime_vulkan_v0_v5_backup
```

CMake globを使用しない。

---

## R26 AI Sonnetへの具体的な実装指示

### 目的

正式経路完成後に、A1～A7、phase/bootstrap、stub、duplicate resource、backup source treeを削除し、Vulkan実装を一つの経路へ収束させる。

### 最初に行う検索

```text
MELONPRIME_SAPPHIRE_VULKAN_
VulkanRendererShellContract
DescribeVulkanRendererShell
CreateSapphireVulkanRenderer3D
CurrentRenderer
GetCurrentRendererOverride
CreateRenderer3DOverrideForSelection
SapphireComposition
CPU safety presentation
ScreenPanelNative::drawScreen
NativeVulkan3DImplemented
ContractVersion
Bootstrap
Phase8
Phase9
Phase10
Phase11
Phase12
Phase13
```

### `GPU_Vulkan.*`

削除:

```text
VulkanRendererShellContract
DescribeVulkanRendererShell
CreateSapphireVulkanRenderer3D
A1～A7 marker
旧compatibility bool
```

正式`VulkanRenderer`とruntime capabilityだけを残す。

### `GPU3D.*`と`Renderer`

削除:

```text
CurrentRenderer ownership
override getter
override setter
override-aware GetRenderer3D
```

通常`Rend3D`所有だけを残す。

### `GPU_Soft.*`

削除:

```text
Vulkan structured frame storage
Vulkan metadata callback orchestration
Vulkan frame serial
Vulkan native-final copies
```

Software renderer本来の処理だけを残す。

### `GPU3D_Vulkan.*`

削除:

```text
Sapphire final composition structs
composition output image
composition descriptor/pipeline/command objects
structured 2D CPU storage
structured 2D GPU upload resource
A3～A7専用getter
```

3D renderer責務だけを残す。

### frontend

`MelonPrimeScreenVulkan`から次を削除する。

```text
CPU safety presentation
Native presenter委譲
空capture
空phase completion callback
常時成功init
```

`VulkanOutput`と`VulkanSurfacePresenter`の重複classを一つへ統合する。

### bootstrap source

実行経路へ不要になった次の種類を削除する。

```text
MelonPrimeVulkan*Bootstrap.cpp
MelonPrimeVulkan*Bootstrap.h
Vulkan_Phase*UiContract
Vulkan_Phase*Stability
旧NativeRaster Vulkan再ラスタライザ
旧Vulkan OutputRing
旧ROM scale bridgeの重複機能
```

正式実装へ必要な機能が残っている場合は、責務ownerへ移してから削除する。

### backup tree

次のdirectoryをsource treeから削除する。

```text
.melonprime_sapphire_vulkan_*_backup
.melonprime_vulkan_v0_v5_backup
.melonprime_phase*_backup
```

patch適用に必要な履歴はGit commitへ任せる。

### CMake

1. 削除fileをtarget sourceから外す。
2. 正式core sourceとfrontend sourceだけを列挙する。
3. globでbackupを拾わない。
4. 重複shader headerを除去する。
5. platform definitionを正式surface adapterへ合わせる。

### include整理

unused include、forward declaration、旧namespace aliasを削除する。

Vulkan API型の露出範囲を再確認する。

### final repository state

```text
Vulkan outer rendererはVulkanRendererだけ
3D rendererはVulkanRenderer3Dだけ
2D producerはVulkanStructured2DProducerだけ
final compositorはVulkanOutputだけ
frame ownerはFrameQueueだけ
window presenterはVulkanSurfacePresenterだけ
```

### 変更してはいけない範囲

- legacy pathを環境変数で再有効化できるよう残さない。
- backup fileへ正式実装を残さない。
- duplicate classを互換aliasで維持しない。
- phase markerを名前だけ変えて残さない。
- Software fallbackをVulkan正常経路へ残さない。

### フェーズ完了時に残す状態

```text
Vulkan実装経路が一つ
A1～A7とphase/bootstrapが不存在
CPU safety presentationが不存在
duplicate compositor/resource ownerが不存在
repository内backup source treeが不存在
```

---


# 32. CMake実装

## 32.1 core target

`MELONPRIME_VULKAN_ACTIVE`内へ次を追加する。

```text
GPU_Vulkan.cpp
GPU2D_Vulkan.cpp
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.cpp
VulkanContext.cpp
VulkanDesktopCompat.cpp
generated 3D shader headers
```

## 32.2 frontend target

```text
MelonPrimeScreenVulkan.cpp
MelonPrimeVulkanSurfaceHost.cpp
VulkanReference/VulkanOutput.cpp
VulkanReference/FrameQueue.cpp
VulkanReference/VulkanSurfacePresenter.cpp
generated compositor/presenter shader headers
```

## 32.3 platform definitions

### Windows

```text
VK_USE_PLATFORM_WIN32_KHR
```

### Linux X11

```text
VK_USE_PLATFORM_XLIB_KHR
```

またはXCB。

### Linux Wayland

```text
VK_USE_PLATFORM_WAYLAND_KHR
```

### macOS

```text
VK_USE_PLATFORM_METAL_EXT
```

## 32.4 shader generation dependency

coreとfrontendが同じgenerated shader targetへ依存する。

生成前headerをsource treeへ手動コピーしない。

## 32.5 build gate

```text
MELONPRIME_ENABLE_VULKAN
MELONPRIME_FORCE_DISABLE_VULKAN
MELONPRIME_ENABLE_VULKAN_MOLTENVK
```

の3段階を維持する。

force disable時はsource、definition、dependency、platform linkを全て除外する。

---

# 33. ファイル別変更一覧

| ファイル | 実装内容 |
|---|---|
| `src/GPU.h` | Vulkan output descriptor、lease、正式renderer output種別 |
| `src/GPU.cpp` | renderer切替順序、override処理削除 |
| `src/GPU3D.h` | CurrentRenderer削除、capture/accelerated hook整理 |
| `src/GPU3D.cpp` | override lifecycle削除 |
| `src/GPU_Soft.h` | Vulkan structured frame storage削除 |
| `src/GPU_Soft.cpp` | Vulkan metadata送信処理削除 |
| `src/GPU2D_Soft.h` | structured output hookのbackend-neutral化 |
| `src/GPU2D_Soft.cpp` | plane/control metadata出力 |
| `src/GPU_Vulkan.h` | 正式`VulkanRenderer` |
| `src/GPU_Vulkan.cpp` | outer renderer lifecycle、output publish |
| `src/GPU2D_Vulkan.h` | structured 2D producer |
| `src/GPU2D_Vulkan.cpp` | 2D metadata frame構築 |
| `src/GPU3D_Vulkan.h` | rc4準拠3D renderer、composition責務削除 |
| `src/GPU3D_Vulkan.cpp` | GPU 3D raster、capture export |
| `src/GPU3D_TexcacheVulkan.*` | GPU texture cache |
| `src/VulkanContext.*` | desktop instance/device/queue共有 |
| `src/VulkanDesktopSurface.*` | platform surface抽象 |
| `src/frontend/qt_sdl/VulkanReference/VulkanOutput.*` | final GPU composition |
| `src/frontend/qt_sdl/VulkanReference/FrameQueue.*` | frame ownership |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*` | swapchain presentation |
| `src/frontend/qt_sdl/MelonPrimeScreenVulkan.*` | Qt接続、layout、HUD |
| `src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp` | 正式VulkanRenderer生成 |
| `src/frontend/qt_sdl/EmuThread.cpp` | override作成削除、backend切替一本化 |
| `src/CMakeLists.txt` | core Vulkan source、shader generation |
| `src/frontend/qt_sdl/CMakeLists.txt` | presenter、surface adapter、platform definition |

---

# 34. 実装順序

実装順序は次で固定する。

```text
R0  偽装contractとactual backend報告を解除
R1  0.7.0.rc4固定ソースを再同期
R2  GPU3D CurrentRenderer overrideを削除
R3  正式なVulkanRendererを実装
R4  VulkanContextをdesktop対応
R5  shader生成を一本化
R6  VulkanRenderer3Dからcomposition責務を除去
R7  render targetを完成
R8  render context ringを完成
R9  accelerated sceneを正式入力化
R10 texture cacheを完成
R11 clear plane / clear bitmap
R12 opaque path
R13 translucent path
R14 shadow path
R15 toon / highlight / edge / fog / AA
R16 display capture
R17 structured 2D producer分離
R18 VulkanOutput final composition
R19 FrameQueue正式接続
R20 desktop surface adapter
R21 VulkanSurfacePresenterとQt接続
R22 factoryとEmuThread一本化
R23 frame lifecycle統合
R24 synchronizationとlifetime
R25 pipeline cache
R26 A1～A7重複実装とstub削除
```

次の順序へ逆転しない。

```text
presenterを先に装飾
GPU3D overrideを残したままfinal compositor追加
SoftRenderer framebufferを正式outputとして維持
A3～A7 compositorとVulkanOutputを併走
```

---

# 35. 各段階の実装完了状態

## R0～R3完了時

```text
Vulkan選択でVulkanRendererが生成される
SoftRendererは生成されない
GPU3D overrideは存在しない
VulkanRendererがVulkanRenderer3Dを所有する
```

## R4～R10完了時

```text
desktop Vulkan deviceが共有される
3D polygonがGPU targetへ描画される
textureがGPU cacheからsampleされる
scaleが3D target寸法へ反映される
```

## R11～R16完了時

```text
clear、opaque、translucent、shadow、toon、edge、fog、captureがGPU経路へ入る
通常表示がSoftware 3Dへ依存しない
```

## R17～R21完了時

```text
2D metadataがVulkanOutputへ渡る
final two-screen imageがGPU上で生成される
Qt presenterがswapchainへ直接表示する
Native CPU presenterを通らない
```

## R22～R26完了時

```text
renderer切替が一つのobject lifecycleになる
旧A1～A7重複resourceが消える
stub、shell、override、CPU safety presentationが消える
0.7.0.rc4型のVulkan経路だけが残る
```

---

# 36. 最終コード構造

```text
Software
    SoftRenderer
    ├─ SoftRenderer2D
    └─ SoftRenderer3D

OpenGL
    GLRenderer
    ├─ GLRenderer2D
    └─ GLRenderer3D / ComputeRenderer3D

Metal
    MetalRenderer
    ├─ Metal 2D
    └─ Metal 3D

Vulkan
    VulkanRenderer
    ├─ VulkanStructured2DProducer
    ├─ VulkanRenderer3D
    ├─ VulkanOutput
    └─ FrameQueue
```

presenter:

```text
NativeQt presenter
    Software output用

OpenGL presenter
    OpenGL output用

Metal presenter
    Metal output用

VulkanSurfacePresenter
    Vulkan output用
```

---

# 37. 最終通常フレーム

```text
GPU3D::RenderPolygonRAM
    ↓
BuildAcceleratedScene
    ↓
VulkanRenderer3D::RenderFrame
    ↓
VkImage ColorImage
    ↓
VulkanStructured2DProducer
    ↓
VulkanOutput::prepareFrameForPresentation
    ↓
VulkanOutput::composeAndSubmitFrame
    ↓
FrameQueue::pushRenderedFrame
    ↓
ScreenPanelVulkan
    ↓
VulkanSurfacePresenter::presentFrame
    ↓
vkQueuePresentKHR
```

この経路にSoftware 3D framebuffer、CPU ownership mask、QImage 3D source、Native painter fallbackを含めない。

---

# 38. 禁止する実装

- Vulkan選択時に`SoftRenderer`をouter rendererとして返す
- `GPU3D::CurrentRenderer`でVulkanを横取りする
- `Renderer::GetRenderer3D()`でglobal overrideを探索する
- Software 3DとVulkan 3Dを通常フレームで併走させる
- `GetLine()`を通常2D合成のため毎scanline呼ぶ
- Vulkan 3DをCPU BGRAへ戻して表示する
- CPU BGRAをVulkan textureへ再uploadする
- `ScreenPanelVulkan::drawScreen()`からNative presenterを呼ぶ
- GPU3D renderer内部へfinal 2D compositorを持たせる
- A3～A7 compositorと`VulkanOutput`を二重所有する
- Compute未実装なのにComputeとして表示する
- `NativeVulkan3DImplemented=true`だけで完成扱いする
- pipeline creation前のshader moduleだけで完成扱いする
- descriptor作成だけでfinal composition完成扱いする
- normal frameで`vkDeviceWaitIdle()`を呼ぶ
- present中frameをrendererが再利用する
- live GPU registerをGUI threadから読む
- live VRAMをpresenterから読む
- scaleをpresenter拡大だけで処理する
- platform surface処理を参照renderer本体へ混ぜる
- backup source treeをbuild対象へ含める

---

# 39. 最終到達点

Vulkan選択時に次が成立する。

```text
3D polygon rasterization        = Vulkan GPU
texture sampling                = Vulkan GPU
depth test                      = Vulkan GPU
stencil                         = Vulkan GPU
translucency                    = Vulkan GPU
shadow                          = Vulkan GPU
toon / highlight                = Vulkan GPU
edge marking                    = Vulkan GPU
fog                             = Vulkan GPU
internal resolution scaling     = Vulkan 3D render target
2D / 3D final composition       = Vulkan GPU
screen swap                     = Vulkan compositor
final two-screen output         = Vulkan image
window presentation             = Vulkan swapchain
normal frame CPU readback       = なし
Software 3D dependency          = なし
Native CPU presenter dependency = なし
```

0.7.0.rc4へ近づける際の中心は、既に存在する個別部品を追加し続けることではない。

中心作業は次である。

```text
参照実装の所有関係
参照実装のframe lifecycle
参照実装のVulkanOutput
参照実装のFrameQueue
参照実装のSurfacePresenter
```

を一つの実行経路として接続し直し、現在の`SoftRenderer + GPU3D override + compositor duplicate + CPU presenter stub`を完全に置き換えることである。

---

# 40. 進捗トラッキング（実施ログ）

参照リポジトリはローカルに固定commitでclone済みで確認した。

```text
melonDS-android-lib @ d77944275fa61f9b79cfcead2c3e98993429a023 （プラン記載のcommitと一致）
melonDS-android     @ tag 0.7.0.rc4 （HEADがtagと一致）
```

| Phase | 状態 | 日付 | 結果メモ |
|---|---|---|---|
| R0 | 完了 | 2026-07-14 | `VulkanRendererShellContract`（`ContractVersion=44`のハードコードbool群）と、それを`ContractVersion==44`等の自己参照で検証するだけの`--melonprime-vulkan-renderer-shell-test`診断（`main.cpp`）を削除。`MelonPrimeRendererFactory.cpp`の`CreateRendererForSelection()`を修正し、Vulkan/VulkanCompute選択時は常に`report.actual=renderer3D_Software`+具体的な`failedStage`/`fallbackReason`を返すようにした（完全な`VulkanRenderer`（R3）が存在するまでVulkanを`actual`として報告しない、というR0 §5.3の規則どおり）。`GPU3D::CurrentRenderer`override自体（R2対象）とcomposition構造体（`SapphireVulkanComposition*`、R6対象）はスコープ外として温存し、範囲を越えなかった。`videoRenderer`の変更検知が`report.actual`ではなく`report.normalized`／configから再導出した値を使っていることを確認し、この修正が既存のrenderer切替検知ロジックを壊さないことを検証済み。 |
| R0b | 完了 | 2026-07-14 | `.claude\skills\build-mingw.bat --vulkan --jobs 1`で`MELONPRIME_ENABLE_VULKAN=ON`の実configure/build/link試行。R0で触った3ファイルのコンパイルはすべて成功したが、最終リンクが`ld.exe: cannot find ...ltrans*.ltrans.o`＋`make.exe: cannot open shared object file: C:`で失敗（2回連続で再現）。原因を特定: `/mingw64/bin`に`make.exe`という名前のバイナリが存在せず（`mingw32-make.exe`のみ）、`-flto=auto`がLTRANS並列化のため内部で`make`をPATH解決すると、`/usr/bin/make.exe`（x86_64-pc-msys向けビルド、MSYS-native）に解決されてしまい、このクロスシェル呼び出し経路では起動時に失敗することを確認。`mingw32-make.exe`を`make.exe`としてshadowするscratch PATH経由で手動検証し、成功（`melonPrimeDS.exe`が生成され、`PE32+ executable ... for MS Windows`と確認）。恒久修正として`build-mingw.bat`と`build-mingw-existing.bat`の両方に、`build/.mingw-make-shim/make.exe`（`mingw32-make.exe`のコピー）を自動生成しPATH先頭に追加する処理を追加し、チェックイン済みスクリプト経由でも同じ修正後の手順で再度configure/build/linkを実行し、`melonPrimeDS.exe`生成を再確認した。ただし、外側の`make`をshadowしても、LTRANS個別unitのコンパイルは内部で`sh.exe`（`/usr/bin/sh.exe`、MSYS-native）をさらに1段spawnしており、そちらは同じ「cannot open shared object file: C:」に引っかかることがあり、ログに`make: [...] Error 256 (ignored)`が数件残った。これはGCCのLTOドライバがparallel LTRANS失敗unitを検出してシリアルにフォールバック処理するためで、最終的な`ld`リンクは成功し、55MBの完全なexeが生成されることを確認済み（サイズ・PE32+ヘッダとも前回の手動shim検証と一致）。つまりshimは「ビルドを完走させる」という目的は達成しているが、環境の根本原因（`/usr/bin/sh.exe`がこのクロスシェル呼び出し経路で不安定）はこのshimだけでは完全には解消していない。この環境で`MELONPRIME_ENABLE_VULKAN=ON`のexeが実際にリンク成功したのは、本セッションで確認した限り初めてである（既存進捗記録は軒並み「ビルド／実機確認待ち」のままだった）。 |
| R1 | 調査完了・実装未着手 | 2026-07-14 | ローカル参照リポジトリと`diff`／`diff -bw`で実比較を実施。line count差だけでは`GPU3D_Vulkan.cpp`が参照比14519対13943行、`diff`が28464行（ほぼ全行不一致に見える）だったが、`diff -bw`（空白無視）では751行まで縮小し、実質的な意味的差分は約5%と判明。`GPU3D_Vulkan.h`も同様（2053→400行）。差分の実体を確認: (1) 意図的で正しい適応 — 参照の`#include <vulkan/vulkan.h>`に対しmelonPrimeDSは`#include <volk.h>`を使用（プランのVulkanDispatch方針どおり）。(2) 意図的で正しい適応 — 参照の`New()`/`Reset(GPU&)`等のシグネチャに対し、melonPrimeDSは`New(GPU3D&)`+zero-arg override（`Reset() override { Reset(GPU); }`）でラップしている。これは本物の`GPU3D.h`の`Renderer3D`基底が実際に zero-arg virtual（`virtual void Reset() = 0;`等）であることを確認した結果、reference Android版のRenderer3D基底とは異なるこのcore向けの必須な適応であり、バグではないと判断。(3) 本当の乖離 — `SapphireVulkanCompositionInput/Resources/CommandContext/DescriptorAbi/ShaderModule`という約110行の構造体一式が`GPU3D_Vulkan.h`にだけ存在し、参照には無い。これはコメント中で自己申告どおり「pipeline creation and dispatch remain deferred」であり、R0のような虚偽の完了主張ではないが、architecturally R6（compositionの責務をVulkanOutputへ戻す）で移設予定の内容と一致した。`GPU2D_Vulkan.cpp/h`（melonPrimeDS独自、参照に対応ファイルなし）、`VulkanContext.cpp/h`（参照800/94行に対しmelonPrimeDS47/36行——大幅な縮小版）、`GPU3D_TexcacheVulkan.cpp/h`（行数同一だが内容はwhitespace差込みでほぼ全行相違——要detailed diff）は次セッションでの継続調査対象として残す。 |

## 重大な計画修正（2026-07-14、R1調査中に発覚）

**本計画の第3章（最終クラス所有関係）およびR2/R3の前提は誤りだった。** 実際に`melonDS-android-lib @ d779442`（本計画が指定する参照commitそのもの）を調査した結果、次が判明した。

```text
grep -rn "^class Renderer\b" melonDS-android-lib/src/*.h  → 該当なし
```

参照コアには、`Renderer2D`と`Renderer3D`は存在するが、両方を束ねる単一の`Renderer`基底クラスは**存在しない**。

```cpp
// melonDS-android-lib/src/GPU3D.h
class GPU3D {
    ...
    [[nodiscard]] Renderer3D& GetCurrentRenderer() noexcept { return *CurrentRenderer; }
    void SetCurrentRenderer(std::unique_ptr<Renderer3D>&& renderer) noexcept;
    ...
private:
    std::unique_ptr<Renderer3D> CurrentRenderer = nullptr;
};

// melonDS-android-lib/src/GPU.h
[[nodiscard]] Renderer3D& GetRenderer3D() noexcept { return GPU3D.GetCurrentRenderer(); }
```

つまり参照アーキテクチャでは、**3Dバックエンドの差し替えは`GPU3D`が単独で所有し**、2D（`GPU2D_Soft : Renderer2D`など）とは完全に独立している。さらに、Android側フロントエンド（`melonDS-android/app/src/main/cpp/renderer/VulkanOutput.cpp`）は次のように`VulkanRenderer3D&`を**直接**受け取っており、melonDSコア側に「Vulkan用の外側Rendererクラス」は一切存在しない。

```cpp
bool VulkanOutput::validateCompositorSubmission(
    Frame* frame, const melonDS::VulkanRenderer3D& renderer3D, int scale, u64 waitTimeoutNs);
```

つまり、GPU-resident 2D合成・`FrameQueue`・swapchainプレゼンテーションはすべて**melonDSコアの外、アプリ／フロントエンド層**に存在し、`GPU3D::CurrentRenderer`経由で得た`VulkanRenderer3D`を直接consumeしている。

### 何が誤りだったか

第3章・第7章・第8章で書いた「`VulkanRenderer : Renderer`が`Rend2D_A/B`・`Rend3D`・`Output`・`FrameQueue`を所有する」という設計は、**desktop upstream melonDS（melonPrimeDSの実際のコア）が持つ`GLRenderer : Renderer`／`MetalRenderer : Renderer`の形を参照アーキテクチャに投影しただけ**であり、参照コード自体には存在しない。

一方、`melonPrimeDS`の現状（`GPU3D::CurrentRenderer`をVulkan専用に`#if defined(MELONPRIME_ENABLE_VULKAN)`でグラフトし、`SoftRenderer`を名目上の外側`Renderer`のままにし、Qt側`VulkanReference/VulkanOutput.*`・`VulkanSurfacePresenter.*`・`MelonPrimeScreenVulkan.*`がAndroidのapp層と同じ役割を担う）は、**desktop側の既存`Renderer`階層を壊さずに参照の実際の設計思想（3D backendはGPU3D単独所有、2D合成・presentationはコア外）を正しく移植した、意図的で妥当なハイブリッド**だったと判断する。

### 撤回・修正する項目

- **R2は撤回する。** `GPU3D::CurrentRenderer`は削除しない。これは参照コアの正規の設計であり、削除するとVulkan統合を参照から乖離させる（退行になる）。
- **R3は「新規`VulkanRenderer : Renderer`をコアに実装する」ではなく、「Qt frontend層（`VulkanReference/VulkanOutput.*`・`VulkanSurfacePresenter.*`・`MelonPrimeScreenVulkan.*`）が、Androidのapp層と同じパターンで`GPU3D`から得た`VulkanRenderer3D&`を直接consumeし、GPU-resident 2D合成・FrameQueue・presentationを完成させる」に読み替える。**
- 第6章・第9章・第18章・第19章・第21章・第22章の「outer VulkanRenderer」への言及は、上記の意味（コアではなくQt frontend層）へ読み替えて解釈すること。次回セッションで該当章を明示的に書き換える。
- `GPU_Vulkan.h/.cpp`（現在`CreateSapphireVulkanRenderer3D`のみ保持）は、このまま「`GPU3D::CurrentRenderer`へ渡す`VulkanRenderer3D`を生成するfactory」の置き場所として妥当。新しい`class VulkanRenderer`をここに追加する必要はない。

### まだ未確定の点（次セッションで調査すべき）

- Android版が2D合成をどう扱っているか（`GPU2D_Soft`のみを常に使い、3DだけをVulkanに差し替えているのか、あるいは2D側にも何らかのVulkan pathがあるのか）を`GPU2D_Vulkan.cpp/h`（melonPrimeDS独自、参照に対応ファイルなし）の実装内容と突き合わせる。melonPrimeDSが独自に`GPU2D_Vulkan`を作った経緯（reference不在のため）が正しい設計判断だったかどうかは要検証。
- `VulkanReference/VulkanOutput.cpp`・`VulkanSurfacePresenter.cpp`が実際に`GPU3D::GetCurrentRenderer()`（または`GPU::GetRenderer3D()`）経由で`VulkanRenderer3D&`を取得しているか、それとも別経路（例えば独自のGPU3D_Vulkan直接参照）を使っているかをコードで確認する。

## R0/R0b 検証済み事実（次セッション向けの前提）

- `MELONPRIME_ENABLE_VULKAN=ON`のconfigure/build/linkは、`.claude\skills\build-mingw.bat --vulkan`（初回）または`.claude\skills\build-mingw-existing.bat`（増分）で、修正後は追加の手動PATH変更なしに成功する。
- `build/.mingw-make-shim/make.exe`は`.gitignore`対象の`build/`配下にあるため、git管理には入らない（ビルドスクリプトが毎回自動生成する）。
- Vulkan選択時、現在は`actual=Software`（正しい・意図的）。UIやログで「Vulkanが動いている」ように見える箇所があれば、それはR3（完全なVulkanRenderer）が無い現状では誤りである。
- `GPU3D_Vulkan.cpp`/`.h`は「書き直し」ではなく「参照からの意味的差分約5%の適応版」。次のR1作業は全面ポートではなく、(a) `SapphireVulkanComposition*`のR6への切り出し、(b) `VulkanContext.cpp/h`の大幅な縮小箇所（800→47行、94→36行）が実際に必要な機能を欠いているかの精査、(c) `GPU3D_TexcacheVulkan`・`GPU2D_Vulkan`の詳細diffに絞れる。

## R3の真の作業量判明（2026-07-14 続き、同日中の追加調査）

R1修正で開いたままだった「まだ未確定の点」2件を実際に調査し、**R3が想定していたよりはるかに大きい**ことが判明した。以下は次セッションが同じ道を辿り直さないための確定事実の記録である。

### 確認手順と結果

1. **`MelonPrimeScreenVulkan.cpp/.h`を全文再読**（前回セッション終了時点から変化なし）。`ScreenPanelNative`を継承した13/16行の正直な空実装で、`initVulkan()`はログを出して`true`を返すだけ、`drawScreen()`は`ScreenPanelNative::drawScreen()`へ委譲するだけ。`GetCurrentRenderer`・`VulkanRenderer3D`・`VulkanOutput`等への参照は**一切ない**（grep 0件。コメント中に"VulkanOutput/VulkanSurfacePresenter"という語がある**だけ**で、実コードからの呼び出しではない）。
2. **`VulkanContext.h/.cpp`を全文再読**（前回「800→47行に大幅縮小」と記録した箇所）。結論を訂正する: これは**スタブではなく実働する完全な実装**。`Acquire()`/`Release()`の参照カウント、instance生成、physical device列挙とスコアリング、queue family探索、timeline semaphore/descriptor indexingのfeature/extension検出、`vkCreateDevice`、`volkLoadDevice`、関数ポインタ解決までを全部行っている。行数が少ないのは1行に複数文を詰めた極めて密な記法のためであり、コメントで自己申告された未完成ではない。Win32用の`VK_KHR_WIN32_SURFACE_EXTENSION_NAME`もinstance拡張に含まれている。R4（非Windows surface拡張の追加）はこのファイルへの**追記**で足り、書き直しは不要と判断する。
3. **`src/frontend/qt_sdl/VulkanReference/`配下を全ファイル確認**。`VulkanOutput.h`（575行）・`VulkanOutput.cpp`（5622行）・`VulkanSurfacePresenter.h`（423行）・`VulkanSurfacePresenter.cpp`（3753行）は、**参照リポジトリの同名ファイルと行数が完全に一致する**（`wc -l`で両方575/5622/423/3753行、双方で確認済み）。つまりこれらは実質的に**参照からの忠実な移植**であり、独自拡張ではない。`FrameQueue.h/.cpp`（各18行）も、terse記法ながら9-frameリングバッファ・free/presentキュー・前フレーム再利用・統計を全部実装した**実働コード**。
4. **`VulkanOutput::prepareFrameForPresentation`の実装を確認**（`.cpp` 2279行目付近）。シグネチャに`const melonDS::GPU& gpu`と`int frontBuffer`を取るが、本体冒頭は`(void)gpu; (void)frontBuffer;`——**両方とも未使用**。関数は呼び出し側が渡す`SoftPackedFrameSnapshot& softPackedSnapshot`が**既に`valid=true`かつ`frontBufferLatched`が0か1にセット済みであることを前提**にしており、`gpu`/`frontBuffer`から何かを計算して埋める処理は一切ない。
5. **参照リポジトリ全体を`SoftPackedFrameSnapshot`で検索**したところ、`renderer/VulkanOutput.h/.cpp`に加えて`app/src/main/cpp/MelonInstance.cpp`（**8058行**）・`MelonInstance.h`（294行）がヒットした。melonPrimeDSには対応する`MelonInstance.cpp`が存在しない。
6. **`MelonInstance.cpp`内の`SoftPackedFrameSnapshot`関連コードを精査**。`MelonInstance::latchSoftPackedFrameSnapshot(...)`という関数（約4819行目〜6200行超、**1400行超**）が、`SoftPackedFrameSnapshot.valid = true`を含む実際のスナップショット構築を行っている唯一の場所。この関数は次を行っている:
   - `previousSoftPackedFrameSnapshot = lastSoftPackedFrameSnapshot;`で前フレーム履歴を保持
   - `frameId`/`frontBufferLatched`/`screenSwapLatched`をセット
   - DSの2D BG/OBJレイヤーを1ピクセルずつplane0/plane1/control配列へ直接エンコード（capture-backed comp4/comp7判定、前フレームとの再利用可否判定、VRAM captureとの合成、class4非対称ケードンス処理などを含む、非常に作り込まれたロジック）
   - `vulkanOutput->buildCompositionInputs(...)`と`vulkanSurfacePresenter->presentFrame(...)`を呼ぶ実際のフレームループ（約1480〜2260行目付近）はこの関数の**外側**にあり、`frameQueue.getRenderFrame()` → `vulkanOutput->captureRenderer3dSnapshot()` → `latchSoftPackedFrameSnapshot()` → `vulkanOutput->prepareFrameForPresentation()` → `buildCompositionInputs()` → `presenter->presentFrame()`という順で呼ばれている。

### 確定した結論

```text
melonPrimeDSにポートされている:
  VulkanContext            (実働・完全)
  GPU3D_Vulkan.h/.cpp       (実働・参照から意味的差分5%程度)
  GPU3D_TexcacheVulkan      (要detailed diff、ただし行数は参照と近い)
  VulkanReference/VulkanOutput.*         (参照と行数完全一致 = 忠実な移植)
  VulkanReference/VulkanSurfacePresenter.* (同上)
  VulkanReference/FrameQueue.*            (同上、terse実装だが実働)

melonPrimeDSにポートされていない（MelonPrimeScreenVulkan.cppが13行のスタブのまま）:
  MelonInstance.cppのVulkan駆動ロジック全体（8058行中の該当部分、
  特にlatchSoftPackedFrameSnapshot が1400行超）
  = フレームループの実際の呼び出し順序
  = SoftPackedFrameSnapshotへ実データを詰める唯一のコード
```

つまり「レンダラー本体（GPU3D_Vulkan）」と「合成・プレゼンテーション本体（VulkanOutput/VulkanSurfacePresenter/FrameQueue）」という**2つの巨大な下位層は実質的に完成しており参照に忠実**だが、両者を実際につなぐ**フレームループの司令塔（Androidでは`MelonInstance.cpp`が担う）が、Qt側では一行も書かれていない**。`MelonPrimeScreenVulkan.cpp`はこの司令塔の置き場所として設計されたはずの場所（コメントがそう示唆している）だが、中身は空。

さらに、`GPU_Soft.cpp`が`SubmitStructured2DLine`/`BeginStructured2DFrame`/`EndStructured2DFrame`経由で`VulkanRenderer3D`（`GPU3D_Vulkan.h`の`Structured2DPlane0/1/Control`等）へ供給している別系統のデータも確認したが、これは`latchSoftPackedFrameSnapshot`とは**別物**であり（参照側に対応する仕組みが見当たらない — melonPrimeDS独自）、`GetStructured2DPlane0()`等のgetterはコードベース全体で**呼び出し元が0件**（宣言のみで未使用）。この系統は現状デッドパイプであり、`latchSoftPackedFrameSnapshot`を移植する際に置き換えられるか、削除対象になる可能性が高い（要判断・R6/R17の対象に含める）。

### R3の再スコープ（正式な訂正）

R3は当初「`MelonPrimeScreenVulkan`を`GPU3D`と`VulkanOutput`/`VulkanSurfacePresenter`に繋ぐ」という比較的小さな配線作業として見積もっていたが、実際には**参照の`MelonInstance.cpp`が持つ8000行規模のVulkan駆動ロジック（特に1400行超の`latchSoftPackedFrameSnapshot`）を、Android JNI環境からQt/デスクトップ環境へ適応移植する**という、本計画の中でも最大級の単一タスクである。

このタスクを次のように段階分割することを推奨する（次セッションでR3を着手する際はこの分割に従うこと）。

- **R3a（最小疎通）**: capture-backed comp4/comp7・前フレーム履歴・class4非対称ケードンス等の高度なケースを**すべて省略**し、`GPU_Soft.cpp`が既に供給している単純な`Structured2DPlane0/1/Control`（またはそれと同等の毎フレーム全画素データ）から`SoftPackedFrameSnapshot`を機械的に構築する最小実装を書く。目的は「何か絵が出るところまで実配線して`presentFrame`を1回でも成功させる」こと。この段階では画質・エッジケース・パフォーマンスは度外視してよい。
- **R3b（機能パリティ）**: `latchSoftPackedFrameSnapshot`の残りのロジック（capture-backed判定、前フレーム再利用、class4ケードンス等）を実際に読み解いて移植し、参照とのパリティを取る。
- **R3c（配線の後始末）**: R3a/R3bが完了した時点で、`GPU3D_Vulkan.h`の`Structured2D*`系デッドパイプ（もしR3aでこれを使わない設計にした場合）と`SapphireVulkanComposition*`（R6対象）を除去する。

R3aだけでも、`Frame`のライフサイクル（`FrameQueue::getRenderFrame`→`pushRenderedFrame`→`getPresentFrame`→`commitPresentedFrame`）、Win32 surfaceの取得（`winId()`から`HWND`、`reinterpret_cast<void*>`で`attachSurface`へ渡す）、`ScreenPanel::emuInstance`（`protected`、`ScreenPanelVulkan`からアクセス可）経由での`EmuInstance::getNDS()`→`nds->GPU.GPU3D.GetCurrentRendererOverride()`→`dynamic_cast<VulkanRenderer3D*>`という一連の配線が必要で、これ自体が独立した検証可能な作業単位になる。

### Structured2Dバッファの実レイアウト（R3a着手前に必読、確認済み）

`GPU3D_Vulkan.h`/`.cpp`を実際に読み、以下を確定した（次セッションはこの節を読めば再調査不要）。

```cpp
// GPU3D_Vulkan.h（メンバ宣言、1029-1035行目付近）
std::array<u32, 2 * 256 * 192> Structured2DPlane0{};
std::array<u32, 2 * 256 * 192> Structured2DPlane1{};
std::array<u32, 2 * 256 * 192> Structured2DControl{};
std::array<u32, 2 * 256 * 192> Structured2DNativeFinal{};
std::array<u32, 2 * 192> Structured2DLineMeta{};   // DispCntの生値
std::array<u32, 2 * 192> Structured2DLineState{};  // MasterBrightness | ScreensEnabled<<16 | EngineEnabled<<17 | ForcedBlank<<18
```

`SubmitStructured2DLine`（`GPU3D_Vulkan.cpp:614`）は`lineIndex = engine*192 + line`、`pixelOffset = lineIndex*256`で書き込む。つまり4つの`2*256*192`配列は「engine 0の192行ぶん（先頭256*192語）→engine 1の192行ぶん（後半256*192語）」という単純な連結レイアウトで、topともbottomとも書いていない（`GPU2D_A`/`GPU2D_B`という**engine**単位）。`GPU_Soft.cpp:117`の`DrawScanline`が示す通り、物理top/bottomへの割り当ては`GPU.ScreenSwap`次第（`!ScreenSwap`ならengine A=top・engine B=bottom、`ScreenSwap`なら逆）。`SoftPackedFrameSnapshot`へ詰める際はこの`ScreenSwap`判定を必ず踏襲すること（`GPU_Soft.cpp`の`dstA`/`dstB`選択と同じ分岐）。

`EndStructured2DFrame`（`GPU3D_Vulkan.cpp:643`）は384行（`2*192`）全部の`Structured2DLineReceived`が立って初めて`Structured2DFrameValid=true`にする——`HasCompleteStructured2DFrame()`はこれを見ればよい。同関数は成功時に`packStructured2DFrame()`+`uploadStructured2DFrameToGpu()`も呼んでおり、これは前述「Sapphire GPU composition」（R6対象・in-core重複pipeline）側の入力になる。**この2系統（CPU向けgetter経由 と GPU buffer経由）は同じ`Structured2D*`ソースを共有しているが、消費先は別**——R3aでCPU側getterを使う設計にするなら、GPU buffer側（`uploadStructured2DFrameToGpu`とその消費者）は使わないことになり、R6でまとめて削除候補にできる。

### R3a着手前に残る唯一の未検証点（次セッションの最初の一歩）

`SubmitStructured2DLine`に渡される`packet.Control`（`GPU_Soft.cpp:171`、`packet.Control = StructuredControl[engine]`）の**ビットレイアウトそのものの生成元**（`GPU_Soft.cpp`のどこで`StructuredControl[engine][x]`へ実際に値を書き込んでいるか、まだ未確認）と、`VulkanOutput`/`VulkanCompositorShader.comp`が`SoftPackedFrameSnapshot::packedTopControl`/`packedBottomControl`に期待するビットレイアウトが**一致するかどうか**を、コードを読んで確認していない。両者は同じ「Control」という名前を持つが、独立に設計された可能性があり、ここを確認せずに`Structured2DControl`をそのまま`packedTopControl`へmemcpyすると、build/linkは通っても構図（3D合成、ウィンドウ、ブレンド等）が壊れた絵になるリスクがある。**次セッションでR3aの実装コードを書く前に、必ずこの2つの「Control」フォーマットを突き合わせてから進めること。** これが今回のセッションでコード実装まで進めなかった理由である。
