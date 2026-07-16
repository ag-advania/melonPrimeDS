# Vulkan S82 監査・修正指示書
## OpenGL無表示 / Software走査線縦複製
## Dead Legacy 2D Path撤去
## Sapphire GPU2D・GPU3D単一Ownership化
## Android / Desktop差分をPresenter・WSI・OpenGL Adapterへ限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `vulkan_sapphire_desktop_rebuild`  
**監査HEAD:** `ee4e4521561db65654498a8e648fb59d13075284`  
**比較基準:** `highres_fonts_v3`  
**比較結果:** 18 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`  
**添付症状:** OpenGLは無表示、Softwareは各DS画面の1本の水平走査線が全行へ縦複製される

---

# 0. 結論

最新HEADの次の修正:

```text
ActiveGPU2DPath:
SapphireCanonical → LegacyOuterRenderer
```

だけではOpenGL / Softwareは直らない。

むしろ現在のVulkan対応buildでは、`LegacyOuterRenderer`側に必要な2D rendererがコンパイル時に除去されている。

現在の実態:

```text
MELONPRIME_ENABLE_VULKAN=ON
    ↓
GPU2D_A / GPU2D_B はSapphire Unitへ全体置換
    ↓
SoftwareRendererの旧Rend2D_A/Bは生成しない
OpenGLRendererの旧GLRenderer2D A/Bも生成しない
    ↓
ActiveGPU2DPathだけLegacyOuterRendererへ戻す
    ↓
Rend->DrawScanline()は呼ばれる
    ↓
しかし2Dを書き込むrendererが存在しない
```

結果:

## Software

```text
未初期化 Output2D[2][256]
    ↓
各scanlineで同じ256 pixelをDrawScanlineA/Bがコピー
    ↓
1本の横線が縦方向へ192回複製
```

添付画像と完全に一致する。

## OpenGL

```text
GLRenderer2D A/Bなし
    ↓
2D shader / texture producerなし
    ↓
final passだけOutputTex2Dを読む
    ↓
blank / 未定義texture
```

したがって、最新commitの説明:

```text
OpenGL and Software rendering broken ... fixed
```

は誤り。

正しくは:

```text
stuckしたstate flagは修正したが、
切替先のLegacy 2D pathがdead pathなので表示回帰は未修正
```

である。

---

# 1. 添付画像の構造解析

画像寸法:

```text
1209 x 1235
```

pixel単位で行比較した結果:

```text
row 4   ～ 621  : 618行が完全一致
row 622 ～ 1234 : 613行が完全一致
```

つまり画面ごとに:

```text
水平1行
    ×
縦方向全行
```

になっている。

これは以下ではない。

```text
- 一般的なRGB/BGR取り違え
- alphaのみの異常
- 画面scaleのnearest-neighbor問題
- Vulkan compositor shaderだけの問題
```

一方、次のコード挙動とは一致する。

```text
Output2Dの256 pixelがlineごとに更新されない
    ↓
各dst rowへ同じOutput2Dをコピー
```

上画面と下画面で複製される横線の内容が異なるのも、

```cpp
Output2D[0][256]
Output2D[1][256]
```

という2本の未初期化bufferを別々に使用する実装と一致する。

---

# 2. P0 root cause
# Softwareの2D producerが存在しない

対象:

```text
src/GPU_Soft.cpp
src/GPU_Soft.h
```

## 2.1 constructor

現在の`SoftRenderer`は、Vulkan対応binaryでは旧2D rendererを生成しない。

```cpp
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    Rend2D_A = std::make_unique<SoftRenderer2D>(...);
    Rend2D_B = std::make_unique<SoftRenderer2D>(...);
#endif
```

つまり今回のbuildでは:

```text
Rend2D_A = null
Rend2D_B = null
```

## 2.2 DrawScanline

Legacy pathへ入った場合:

```cpp
Rend->DrawScanline(line);
```

が呼ばれる。

しかし`SoftRenderer::DrawScanline()`内のproducerも同じguardで除外されている。

```cpp
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    Rend2D_A->DrawScanline(line);
    Rend2D_B->DrawScanline(line);
#endif
```

その直後は無条件で:

```cpp
DrawScanlineA(line, dstA);
DrawScanlineB(line, dstB);
```

が呼ばれる。

## 2.3 Output2D

`GPU_Soft.h`:

```cpp
alignas(8) u32 Output2D[2][256];
```

初期値なし。

`Rend2D_A/B`が存在しないため、
この配列へ有効なscanlineを書き込むownerがいない。

それでも`DrawScanlineA/B`は:

```cpp
*(u64*)&dst[i] = *(u64*)&Output2D[screen][i];
```

相当のcopyを行う。

これが添付画像の直接原因。

---

# 3. P0 root cause
# OpenGLの2D texture producerが存在しない

対象:

```text
src/GPU_OpenGL.cpp
src/GPU_OpenGL.h
src/GPU2D_OpenGL.cpp
src/GPU2D_OpenGL.h
```

## 3.1 constructor

Vulkan対応binaryでは:

```cpp
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    Rend2D_A = std::make_unique<GLRenderer2D>(...);
    Rend2D_B = std::make_unique<GLRenderer2D>(...);
#endif
```

が除外される。

## 3.2 Init

以下も除外される。

```text
GLRenderer2D::InitShaders
GLRenderer2D::Init
```

## 3.3 scanline / sprite

以下も除外される。

```text
Rend2D_A->DrawScanline
Rend2D_B->DrawScanline
Rend2D_A->DrawSprites
Rend2D_B->DrawSprites
```

## 3.4 final pass

一方、final passは引き続き:

```text
OutputTex2D[0]
OutputTex2D[1]
```

をbindして描画する。

2D textureを生成・更新する`GLRenderer2D`が存在しないため、
final passへの2D sourceが成立しない。

OpenGL無表示は自然な結果。

---

# 4. 最新commitの誤診

最新HEAD:

```text
ee4e4521561db65654498a8e648fb59d13075284
```

commitは:

```text
DeactivateSapphireVulkan2D()
    ↓
ActiveGPU2DPath = LegacyOuterRenderer
```

を追加した。

state machine上の非対称性を直した点自体は正しい。

しかし修正後の分岐先:

```text
LegacyOuterRenderer
```

がVulkan対応binaryでは機能しない。

したがって評価は:

| 項目 | 評価 |
|---|---|
| SapphireCanonicalへの固着解除 | 修正済み |
| Software 2D producer復元 | 未修正 |
| OpenGL 2D producer復元 | 未修正 |
| renderer切替自動テスト | 未実装 |
| 実画面確認 | 未検証 |
| commitの「fixed」表現 | 不正確 |

Phase 4文書の:

```text
OpenGL/Software rendering broken ... root-caused and fixed
```

はreopenすること。

---

# 5. 根本原因は「半分だけSapphire」

現在のGPU object model:

```text
GPU
 ├─ SapphireGPU2DCore::GPU2D::Unit A
 ├─ SapphireGPU2DCore::GPU2D::Unit B
 ├─ Sapphire GPU2D Renderer
 ├─ SapphireGpu2DState
 ├─ ActiveGPU2DPath
 │
 ├─ legacy outer Renderer
 │    ├─ legacy Software/OpenGL Renderer3D
 │    └─ legacy Renderer2DはVulkan buildでは削除
 │
 └─ GPU3D::CurrentRenderer
      └─ 主にVulkanだけが使用
```

これはSapphire本家のownershipではない。

Sapphire本家:

```text
GPU
 ├─ GPU2D::Unit A
 ├─ GPU2D::Unit B
 ├─ unique_ptr<GPU2D::Renderer2D>
 ├─ GPU3D
 │    └─ unique_ptr<Renderer3D>
 └─ framebuffer
```

2Dと3Dのrenderer ownerが一つのGPU object modelに統一されている。

現在のmelonPrimeDSは:

```text
2D UnitはSapphire
3D ownerはbackendごとに分裂
presentationはouter renderer
```

となっている。

このため:

```text
Vulkan:
    Sapphire 2Dは動くがsurface generationが欠落

Software:
    Legacyへ戻すと2D producerがない

OpenGL:
    Legacyへ戻すとGL 2D producerがない

exact pin:
    class GPU2Dとnamespace GPU2Dが衝突
```

という3種類の障害が同時発生している。

---

# 6. P0
# GPU3D ownershipも統一されていない

現在:

## Vulkan

```text
GPU3D::CurrentRenderer = VulkanRenderer3D
```

## Software / OpenGL

`GPU::SetRenderer()`で:

```cpp
GPU3D.SetCurrentRenderer(nullptr);
```

となり、3D rendererはouter `SoftRenderer` / `GLRenderer`の`Rend3D`側に残る。

Sapphire GPU2D rendererは:

```text
GPU.GPU3D.GetCurrentRenderer()
GPU.GPU3D.GetLine()
GPU.GPU3D.IsRendererAccelerated()
```

を参照する。

したがって単純に:

```text
Software/OpenGLでもSapphire GPU2Dを常時activeにする
```

だけでは不十分。

Software/OpenGLのRenderer3Dも:

```text
GPU3D::CurrentRenderer
```

へ移す必要がある。

---

# 7. P1
# 非accelerated framebuffer sizeもSapphireと不一致

現在:

```cpp
if (GPU3D.IsRendererAccelerated())
    return (256 * 3 + 1) * 192;
return SoftRenderer::kPackedFramebufferPixels;
```

ところが:

```cpp
SoftRenderer::kPackedFramebufferPixels
    = (256 * 3 + 1) * 192;
```

なので、Softwareでも常に769 stride相当を確保する。

Sapphire本家:

```text
accelerated   : (256*3+1) * 192
nonaccelerated: 256 * 192
```

修正:

```cpp
return GPU3D.IsRendererAccelerated()
    ? (256u * 3u + 1u) * 192u
    : 256u * 192u;
```

ただしOpenGL 2D adapterがCPU framebufferを使わない場合は、
adapter capabilityで明示的に決めること。

---

# 8. P1
# exact pinは依然OFF

現状:

```text
MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF
```

理由:

```text
melonDS::GPU2D
```

が、

```text
現コード: class
Sapphire: namespace
```

で衝突する。

これはSapphireの問題ではない。

melonPrimeDSがupstream object modelをflattenしたために生じた移植差分。

「純Sapphire」を目標にするなら、
Sapphire側を別名へ逃がし続けるより、
production object modelをupstreamへ合わせる方が正しい。

---

# 9. P1
# raw vendorを直接編集しない

現在は:

```text
src/SapphireVendor/upstream/melonDS-android-lib/src/GPU2D.h
```

へinclude guard変更が入っている。

理由が「manifestでSHA pinしていないため安全」であっても、
pure vendor運用としては不適切。

正しい境界:

```text
SapphireVendor/raw/
    upstream fileそのまま
    byte-identical
    SHA固定
    手編集禁止

SapphireGenerated/
    namespace変換
    include path変換
    Desktop adapter hook
    generator出力
```

許可する変換:

```text
namespace prefix
include path
symbol prefix
platform hook declaration
volk dispatch
WSI boundary
```

許可しない変換:

```text
描画algorithm
capture判定
screen swap
stride semantics
queue order
temporal判定
```

---

# 10. P1
# Vulkanは依然surfacePresentできない

cold-start crashとshutdown crashは修正された。

保持すべき修正:

```text
- generated OBJECT libraryをcoreへlinkし、
  JIT_ENABLED等のclass-layout macroを一致させるODR修正

- ScreenPanelVulkan::beginClose()で
  frontend sessionを同期unregisterするshutdown順序修正
```

ただしVulkan presentは依然:

```text
frame.surfaceGeneration = 0
surfaceHost.generation() = 1以上
```

となり、

```text
surfaceGenMismatch
```

で全frame defer。

原因:

```text
DesktopFrameLifetimeTracker.cpp
```

だけが`surfaceGeneration`を設定していたのに、
rebuild pathではCMakeから除外したため。

FrameQueue exact coreへDesktop generation logicを入れてはいけない。

修正先:

```text
DesktopPresentationLease
DesktopSurfaceFrameTag
Presenter adapter
```

のいずれか。

Sapphire FrameQueueはqueue selectionだけを所有する。

---

# 11. 目標architecture

## 11.1 core

```text
GPU
 ├─ exact Sapphire GPU2D::Unit A/B
 ├─ exact Sapphire GPU2D::Renderer2D owner
 ├─ GPU3D::Renderer3D owner
 └─ exact Sapphire framebuffer lifecycle
```

## 11.2 backend pair

```text
Software:
    Renderer2D = Sapphire SoftRenderer
    Renderer3D = Software Renderer3D
    Presentation = NativeQt CPU BGRA

OpenGL:
    Renderer2D = Desktop GLRenderer2D Adapter
                 implementing Sapphire Renderer2D interface
    Renderer3D = OpenGL / Compute Renderer3D
    Presentation = OpenGL

Vulkan:
    Renderer2D = Sapphire SoftRenderer
    Renderer3D = Sapphire VulkanRenderer3D
    Presentation = Sapphire VulkanOutput + Desktop WSI
```

## 11.3 Desktop差分

Desktop側へ残してよいもの:

```text
Win32/X11/Wayland/macOS surface
Qt window lifecycle
volk loader
filesystem
logging
presentation timing
HUD
OpenGL-specific Renderer2D implementation
```

coreへ入れてはいけないもの:

```text
backendごとのGPU2D register state
ActiveGPU2DPath
duplicate framebuffer ownership
duplicate capture state
Desktop queue selection
Desktop screen-swap heuristic
```

---

# 12. OpenGLの修正方針

OpenGLはAndroid Sapphireに存在しないDesktop backendなので、
Desktop差分としてadapterが必要。

ただし新規rendererを書き直さない。

既存:

```text
GPU2D_OpenGL.cpp
GPU2D_OpenGL.h
```

を機械的にcanonical interfaceへ移植する。

## 現在

```cpp
class GLRenderer2D : public legacy Renderer2D
{
    GLRenderer2D(melonDS::GPU2D& gpu2D, GLRenderer& parent);
    void DrawScanline(u32 line);
    void DrawSprites(u32 line);
};
```

## 目標

```cpp
class GLRenderer2D final : public GPU2D::Renderer2D
{
public:
    explicit GLRenderer2D(GPU& gpu, GLRenderer& parent);

    void DrawScanline(u32 line, GPU2D::Unit* unit) override;
    void DrawSprites(u32 line, GPU2D::Unit* unit) override;
    void VBlankEnd(GPU2D::Unit* unitA, GPU2D::Unit* unitB) override;
};
```

原則:

```text
- register値はcanonical Unitから読む
- stateを複製しない
- LegacyGPU2D shadow objectを作らない
- OpenGL shader logicは既存コードを移植
- interface変換以外のalgorithm変更をしない
```

これは車輪の再発明ではなく、
既存Desktop OpenGL rendererをSapphire object modelへ接続するadapter化。

---

# 13. Softwareの修正方針

Softwareでは旧outer 2D pathを使わない。

Sapphire SoftRendererはnonaccelerated時に既に:

```text
- BG/OBJ描画
- display mode処理
- capture
- master brightness
- 32-bit BGRA変換
```

を行い、canonical framebufferへ直接書く。

したがってrebuild pathのSoftwareでは削除対象:

```text
SoftRenderer::Output2D
SoftRenderer::DrawScanlineA
SoftRenderer::DrawScanlineB
legacy SoftRenderer2D A/B
```

Desktop `SoftRenderer`は最終的に:

```text
canonical framebufferをNativeQtへ公開するadapter
```

だけにする。

---

# 14. ActiveGPU2DPathを廃止する

現在の:

```text
LegacyOuterRenderer
SapphireCanonical
```

というruntime switchは、
2つの2D emulation systemが存在する前提。

目標architectureでは2D emulation systemは1つだけ。

したがって:

```text
ActiveGPU2DPath
ActivateSapphireVulkan2D
DeactivateSapphireVulkan2D
UsesSapphireGpu2DPath
```

をrenderer selectorとして使わない。

必要なのは次だけ。

```text
Structured Vulkan metadata publication enabled?
Vulkan renderer generation?
Vulkan compositor publication valid?
```

例:

```cpp
bool GPU::UsesStructured2DOutput() const noexcept
{
    return GPU3D.HasCurrentRenderer()
        && GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();
}
```

backend切替時:

```text
GPU2D rendererは変えない
GPU2D Unit stateも変えない
Renderer3Dだけ交換
framebufferをcapabilityに合わせて再確保
structured publicationだけinvalidate
```

---

# 15. 修正phase

---

## S82-0 — 誤った完了記録をreopen

更新:

```text
PHASE4_COLD_START.md
Vulkan_純Sapphire基準_Desktop再構築方針.md
```

変更:

```text
OpenGL/Software fixed
    ↓
root cause partially identified;
state flag fixed but Legacy 2D path is dead
```

Phase 4:

```text
partial / red
```

維持。

推奨commit:

```text
S82-0: Reopen OpenGL and Software rendering regression
```

---

## S82-1 — 表示回帰testを先に追加

### Software fixture

rowごとに異なるdeterministic patternを描くROMを使用。

assert:

```text
width = 256
height = 192
distinct row hashes >= 64
top row hash != middle row hash
middle row hash != bottom row hash
```

単純なblank frameでは判定しない。

### OpenGL fixture

offscreen FBO readback。

assert:

```text
OutputTex2D handles created
top/bottom framebuffer non-empty
distinct row hashes >= 64
expected reference hash
```

### transition matrix

```text
Software -> Vulkan -> Software
OpenGL   -> Vulkan -> OpenGL
Vulkan   -> Software
Vulkan   -> OpenGL
Software -> OpenGL
OpenGL   -> Software
```

各transition後に最低10frame確認。

推奨commit:

```text
S82-1: Add renderer output and transition regression tests
```

---

## S82-2 — dead Legacy pathをfail-fast化

本修正が完了するまで、
未初期化bufferを表示して成功扱いにしない。

Debug:

```cpp
assert(!(legacyPathSelected && legacy2DRendererUnavailable));
```

Release:

```text
renderer initialization failure
explicit fallback
error log
```

禁止:

```text
Output2Dを0初期化して黒画面にするだけ
```

これは症状隠蔽。

推奨commit:

```text
S82-2: Reject renderer paths without a live 2D producer
```

---

## S82-3 — production GPU2D object modelをSapphireへ統一

作業:

```text
class melonDS::GPU2D
    ↓
Sapphire同様:
namespace melonDS::GPU2D
class Unit
class Renderer2D
```

既存normalized namespace:

```text
SapphireGPU2DCore::GPU2D
```

も最終的にupstream namespaceへ近づける。

変換はgeneratorで管理。

推奨commit:

```text
S82-3: Adopt Sapphire GPU2D Unit and Renderer2D object model
```

---

## S82-4 — GPU3D ownerをGPUへ統一

`RendererCreationResult`をrenderer pairへ変更。

```cpp
struct RendererCreationResult
{
    std::unique_ptr<GPU2D::Renderer2D> Renderer2D;
    std::unique_ptr<Renderer3D> Renderer3D;
    std::unique_ptr<PresentationAdapter> Presentation;
};
```

Software/OpenGL/Vulkanすべて:

```cpp
GPU.SetRenderer2D(...)
GPU.SetRenderer3D(...)
```

を通す。

outer Rendererが独自に`Rend3D`を持つ構造を廃止方向へ移す。

推奨commit:

```text
S82-4: Make GPU3D the sole owner of every backend Renderer3D
```

---

## S82-5 — SoftwareをSapphire SoftRendererへ直結

作業:

```text
GPU2D Renderer = exact/normalized Sapphire SoftRenderer
GPU3D Renderer = SoftRenderer3D
Framebuffer = 256x192 BGRA
NativeQt presenter = canonical framebuffer
```

削除:

```text
Output2D read
DrawScanlineA/B double composition
legacy Rend2D_A/B dependency
```

acceptance:

```text
添付の縦縞再現なし
192行がfixture期待値
Vulkanを一度選択後もSoftware正常
```

推奨commit:

```text
S82-5: Drive Software output directly from Sapphire GPU2D
```

---

## S82-6 — OpenGL Renderer2Dをcanonical Unitへadapter化

作業:

```text
既存GPU2D_OpenGL algorithmを維持
constructor/interfaceだけcanonical化
register readをUnitへ接続
```

禁止:

```text
LegacyGPU2D Unitを裏で複製
registerを毎frameコピー
別のcapture state machineを追加
```

acceptance:

```text
GLRenderer2D A/B created
2D shaders initialized
OutputTex2D populated
OpenGL/Compute両方表示
renderer切替後も表示
```

推奨commit:

```text
S82-6: Port the existing OpenGL 2D renderer to Sapphire Unit
```

---

## S82-7 — framebuffer sizingをSapphireへ一致

Software:

```text
256 * 192
```

Vulkan structured:

```text
(256 * 3 + 1) * 192
```

buffer type/capabilityを明示。

例:

```cpp
enum class GPU2DFramebufferLayout
{
    NativeBgra256,
    SapphireStructured769,
    ExternalTexture
};
```

algorithm内でbackend IDを直接判定しない。

推奨commit:

```text
S82-7: Match Sapphire framebuffer layout by renderer capability
```

---

## S82-8 — ActiveGPU2DPathを撤去

移行後:

```text
GPU2D renderer = always valid
GPU2D execution = always canonical
```

Vulkan切替で変更するのは:

```text
Renderer3D
structured publication
surface/presenter state
```

だけ。

推奨commit:

```text
S82-8: Remove the dual GPU2D execution-path state machine
```

---

## S82-9 — Vulkan surfaceGenerationをDesktop adapterで所有

追加候補:

```cpp
struct DesktopSurfaceFrameTag
{
    u64 rendererGeneration;
    u64 surfaceGeneration;
};
```

producer completion時にframe resource leaseへ付与。

present時にadapterが照合。

禁止:

```text
Sapphire FrameQueue coreへsurface generation field selection logicを追加
```

FrameQueueはSapphireそのまま。

acceptance:

```text
frame.surfaceGeneration != 0
frame.surfaceGeneration == live generation
surfacePresent=1
splash hidden
```

推奨commit:

```text
S82-9: Tag Sapphire frames with Desktop surface generation outside FrameQueue
```

---

## S82-10 — raw vendorをbyte-identical化

構造:

```text
SapphireVendor/raw/
SapphireGenerated/
```

CI:

```text
raw SHA == upstream SHA
git diff raw == empty
generated == generator output
```

現在raw vendorへ入れたinclude guard変更はgenerator変換へ移動。

推奨commit:

```text
S82-10: Keep raw Sapphire vendor sources byte-identical
```

---

## S82-11 — exact pinを本当にbuild可能にする

推奨順:

### 1

productionをupstream namespaceへ合わせる。

### 2

exact raw sourceを直接compile。

### 3

Desktop hookのみ薄いadapterで注入。

避ける:

```text
Sapphire全体を別namespaceへ大量手修正
```

どうしてもnamespace isolationする場合は、
完全自動generatorとtoken-level parity testを必須にする。

acceptance:

```text
MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=ON
full core build success
melonPrimeDS link success
Software/OpenGL/Vulkan tests success
```

推奨commit:

```text
S82-11: Build the production core with exact pinned Sapphire GPU2D
```

---

## S82-12 — old outer renderer ownershipを削除

CMakeから段階的に除外:

```text
legacy GPU2D owner
legacy duplicate framebuffer
legacy duplicate 3D owner
dead compatibility branches
```

presentation-only adapterは残してよい。

推奨commit:

```text
S82-12: Remove obsolete duplicate renderer ownership
```

---

## S82-13 — parity testをdependency closureへ拡張

比較対象:

```text
GPU2D.h
GPU2D.cpp
GPU2D_Soft.h
GPU2D_Soft.cpp
GPU.cpp lifecycle regions
GPU3D owner lifecycle
FrameQueue
FrameLatch
VulkanOutput
VulkanSurfacePresenter
shader sources
```

許容diffをmanifestへ列挙。

`desktop_adapter_exempt`へ大きなproduction fileを丸ごと指定しない。

function/region単位で許容する。

推奨commit:

```text
S82-13: Verify the complete Sapphire renderer dependency closure
```

---

## S82-14 — rebuild CIを実行

現在HEADにはcombined statusがなく、
connectorからworkflow runも確認できない。

追加:

```yaml
push:
  branches:
    - vulkan_sapphire_desktop_rebuild
```

matrix:

```text
Windows Release
Windows Debug
Linux Debug
Linux ASan/UBSan
```

backend matrix:

```text
Software
OpenGL
OpenGL Compute
Vulkan
```

transition matrixも実行。

推奨commit:

```text
S82-14: Run all renderer and transition tests on rebuild CI
```

---

# 16. 最短hotfixと本修正を混同しない

## 許容する短期措置

```text
- dead path検出
- Softwareだけ先にSapphire canonicalへ移行
- OpenGLを明示的にunavailable扱いにして誤表示を防ぐ
```

ただしrelease完成扱いにはしない。

## 禁止する短期措置

```text
- Output2Dをmemsetして黒画面にする
- 前frameを延々再利用
- 1行目を192回copyする処理を別名で残す
- ActiveGPU2DPathへ3つ目の状態を追加
- LegacyGPU2D shadow stateを作る
- Vulkanを選択したことがあるかの履歴flagを追加
- OpenGLだけ別のregister mirrorを持つ
- Software fallbackで問題を隠す
```

---

# 17. 推奨commit順

```text
S82-0  Reopen incorrect fixed status
S82-1  Add output/transition regression tests
S82-2  Fail fast on missing 2D producer
S82-3  Adopt Sapphire GPU2D object model
S82-4  Unify GPU3D ownership
S82-5  Restore Software via Sapphire SoftRenderer
S82-6  Adapt existing OpenGL 2D renderer to Sapphire Unit
S82-7  Correct framebuffer layouts
S82-8  Remove ActiveGPU2DPath
S82-9  Fix Desktop surface generation tagging
S82-10 Make raw vendor byte-identical
S82-11 Enable exact pinned production build
S82-12 Remove duplicate renderer ownership
S82-13 Expand parity closure
S82-14 Enable full rebuild CI
```

各commitで:

```text
build
targeted test
backend transition test
```

を実行してから次へ進む。

複数phaseを1commitへまとめない。

---

# 18. 完了条件

## Software

```text
- 256x192 canonical framebuffer
- deterministic fixture hash一致
- 同一row縦複製なし
- Vulkan選択後に戻しても正常
```

## OpenGL

```text
- GLRenderer2D A/Bがcanonical Unitを使用
- OutputTex2Dが実際に生成・更新される
- OpenGL / Compute両方表示
- Vulkan切替後も正常
```

## Vulkan

```text
- exact Sapphire GPU2D
- exact FrameQueue / FrameLatch
- surface generation一致
- surfacePresent=1
- splash hidden
```

## Ownership

```text
- GPU2D register stateは1組だけ
- GPU2D renderer ownerは1つ
- GPU3D renderer ownerは1つ
- framebuffer ownerは1つ
- ActiveGPU2DPathなし
```

## Vendor

```text
- raw source byte-identical
- generated transformation reproducible
- exact pin ONでfull build
```

## CI

```text
- latest HEADへstatusあり
- rebuild branch pushで実行
- Software/OpenGL/Vulkan matrix green
- transition matrix green
```

---

# 19. 最終判断

今回の画像は偶発的なGPU corruptionではない。

コード上、Vulkan対応buildのLegacy pathから:

```text
Software 2D producer
OpenGL 2D producer
```

を除外した状態で、
最新commitがそのLegacy pathへ制御を戻した結果である。

直接原因:

```text
Software:
uninitialized Output2Dの同一scanline反復

OpenGL:
GLRenderer2D不在による2D texture source欠落
```

根本原因:

```text
Sapphire Unitへ移行しながら、
renderer ownershipとpresentation ownershipを旧architectureに残した半移植
```

修正の中心は新しいworkaroundではない。

```text
SapphireのUnit / Renderer2D / Renderer3D ownershipをそのまま採用し、
Desktop差分をOpenGL renderer adapterとWSI/presenterへ限定する
```

ことである。

最新の`ActiveGPU2DPath`修正は残す価値のある小修正ではあるが、
最終architectureでは`ActiveGPU2DPath`自体を削除する。

この状態を「OpenGL/Software修正済み」または
「純Sapphire Phase 4完了」と扱ってはいけない。
