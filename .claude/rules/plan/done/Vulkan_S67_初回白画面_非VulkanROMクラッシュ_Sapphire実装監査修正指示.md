# Vulkan初回ROM白画面・非Vulkan renderer ROM起動クラッシュ
## Sapphire GPU2D実行境界／state同期監査・修正指示書（S67）

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `aaed0869922961270c4c58e910ec07713ba36ca1`  
**前回監査HEAD:** `16426bf6ca840d6b53be373c0db9eb873e866589`  
**差分:** 9 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 症状

```text
1. Vulkanを選択した状態でROMを開くと白画面が続く
2. Software／OpenGL等、他rendererを選択してROMを開くと落ちる
3. 前回S66でSapphire GPU2D stateのpersistent化を実装した後に発生
```

---

# 1. 監査結論

## 1.1 現在の実装はSapphireの描画algorithmを移植しているが、Sapphireと同じruntime ownershipではない

Sapphire本家では、`GPU2D::Unit`がGPU2D register stateの正規所有者である。

現在のMelonPrimeDSには、同じGPU2D状態が二重に存在する。

```text
native melonDS:
    GPU2D_A
    GPU2D_B

Sapphire mirror:
    SapphireGpu2DState::UnitA
    SapphireGpu2DState::UnitB
```

register writeを両方へ送って同期させているが、

```text
Power state
Reset
Direct Boot
savestate load
renderer selection
VBlank
FIFO
capture
```

の一部は二重stateへ同じ順序で適用されていない。

したがって、現状は:

```text
Sapphire pixel algorithmの移植
```

ではあるが、

```text
Sapphire state machine／lifecycleの移植
```

にはなっていない。

---

# 2. P0
# 非Vulkan rendererクラッシュの確定原因

## 2.1 `MELONPRIME_ENABLE_VULKAN`をruntime backend判定として使用している

現在の`SoftRenderer` constructorは、Vulkan対応buildでは常にnative 2D rendererを作らない。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Rend2D_A／Rend2D_Bを作らない
#else
    Rend2D_A = std::make_unique<SoftRenderer2D>(...);
    Rend2D_B = std::make_unique<SoftRenderer2D>(...);
#endif
```

同様に`DrawScanline()`と`DrawSprites()`も、
Vulkan対応buildでは選択中rendererに関係なくSapphire 2Dを実行する。

```text
Vulkanがbuildされている
    ↓
Softwareを選択していても
    ↓
SapphireGPU2DCore::SoftRenderer::DrawScanline()
```

これはbuild capabilityとruntime selectionの混同である。

---

## 2.2 Software選択時は`GPU3D::CurrentRenderer == nullptr`

renderer transactionは全backendで次を呼ぶ。

```cpp
nds->GPU.SetRenderer3D(
    std::move(result.Renderer3D));
```

Software／OpenGL／Metalでは、
`result.Renderer3D`は通常`nullptr`である。

したがって:

```text
Software outer SoftRenderer
native Rend3D = SoftRenderer3D
GPU3D::CurrentRenderer = nullptr
```

となる。

外側`Renderer::GetRenderer3D()`はnative `Rend3D`へfallbackできるが、
Sapphire coreは直接`GPU.GPU3D.GetCurrentRenderer()`を使用している。

---

## 2.3 deterministic null dereference

Sapphire移植側の現在の実装:

```cpp
bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return GPU.GPU3D.GetCurrentRenderer()
        .UsesStructured2DMetadata();
}
```

`GetCurrentRenderer()`は:

```cpp
return *CurrentRenderer;
```

であり、null guardを持たない。

Software ROM起動時:

```text
CurrentRenderer == nullptr
    ↓
SoftRenderer outerがSapphire DrawScanlineを呼ぶ
    ↓
UseStructuredVulkan2D()
    ↓
*CurrentRenderer
    ↓
segmentation fault
```

この経路はコード上で確定している。

---

# 3. P0修正
# Sapphire 2D実行をactive Vulkan graphics backendに限定する

## 3.1 central runtime gateを1個だけ作る

散在する`#ifdef`や`dynamic_cast`を増やさず、
desktop compatibility seamへ集約する。

推奨場所:

```text
VulkanDesktopCompat.h
VulkanDesktopCompat.cpp
```

または:

```text
MelonPrimeSapphireGpu2DState
```

実装例:

```cpp
bool SapphireGpu2DState::IsActiveForRendering() const noexcept
{
    if (!GPU.GPU3D.HasCurrentRenderer())
        return false;

    const Renderer3D& renderer =
        GPU.GPU3D.GetCurrentRenderer();

    return renderer.UsesStructured2DMetadata();
}
```

現在の実装では`UsesStructured2DMetadata()`をtrueにするのは
Vulkan graphics rendererだけなので、この判定を正規gateにできる。

将来別backendが同じflagを使用する場合は、
`Renderer3DBackendKind`等の明示backend identityを追加する。

---

## 3.2 `UseStructuredVulkan2D()`を必ずnull-safeにする

最小修正:

```cpp
bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return GPU.GPU3D.HasCurrentRenderer()
        && GPU.GPU3D.GetCurrentRenderer()
               .UsesStructured2DMetadata();
}
```

ただし、これはクラッシュ防止だけであり、
非Vulkan時にSapphire renderer自体を実行し続けてよいという意味ではない。

---

## 3.3 native `Rend2D_A/B`を復元する

`SoftRenderer`ではVulkan buildでもnative 2D rendererを保持する。

```cpp
SoftRenderer::SoftRenderer(NDS& nds)
    : Renderer(nds.GPU)
{
    // framebuffer allocation

    Rend2D_A =
        std::make_unique<SoftRenderer2D>(
            GPU.GPU2D_A, *this);

    Rend2D_B =
        std::make_unique<SoftRenderer2D>(
            GPU.GPU2D_B, *this);

    Rend3D =
        std::make_unique<SoftRenderer3D>(
            GPU.GPU3D, *this);
}
```

Sapphire 2Dはpersistent `SapphireGpu2DState`側に既に存在するため、
両方を保持してもalgorithm duplicationにはならない。

```text
Software runtime:
    upstream melonDS SoftRenderer2D

Vulkan graphics runtime:
    upstream Sapphire GPU2D SoftRenderer
```

という明確な選択にする。

---

## 3.4 `DrawScanline()`をruntime branchにする

```cpp
void SoftRenderer::DrawScanline(u32 line)
{
#if defined(MELONPRIME_DS) \
 && defined(MELONPRIME_ENABLE_VULKAN)
    if (auto* state =
            GPU.TryGetSapphireGpu2DState();
        state != nullptr
        && state->IsActiveForRendering())
    {
        BindSapphirePhysicalTargets();

        line = GPU.VCount;
        if (line < 192)
        {
            state->Renderer.DrawScanline(
                line, &state->UnitA);

            state->Renderer.DrawScanline(
                line, &state->UnitB);
        }
        return;
    }
#endif

    // ここから下はupstream melonDSの
    // original SoftRenderer::DrawScanlineをそのまま維持
}
```

同じruntime gateを:

```text
DrawSprites
VBlank
VBlankEnd
PublishSapphire2DFrame
RefreshSapphireVulkanBindings
structured capture access
```

にも適用する。

---

# 4. P0
# Vulkan白画面の直接原因

## 4.1 Sapphire Unitがdisabledのままregister writeを受けている

Direct Bootでは:

```cpp
PowerControl9 = 0x820F;
GPU.SetPowerCnt(PowerControl9);
```

が実行される。

現在の`GPU::SetPowerCnt()`がEnableにするのはnative側だけである。

```cpp
GPU2D_A.SetEnabled(...);
GPU2D_B.SetEnabled(...);
```

Sapphire側:

```text
UnitA.Enabled = false
UnitB.Enabled = false
```

のまま残る。

---

## 4.2 forwarding順序によりwriteが消失する

native `GPU2D::Write8/16/32()`は、
native stateを更新する前にSapphireへforwardする。

```cpp
ForwardRegisterWrite16(...);

switch (...)
{
    // native write
}
```

Sapphire `Unit::Write8/16/32()`は、
DISPCNT等の一部を除き:

```cpp
if (!Enabled)
    return;
```

する。

したがってDirect Boot直後:

```text
native GPU2D.Enabled = true
Sapphire Unit.Enabled = false

ゲームがBGCNT、BG scroll、window、
blend、mosaic等を書込む
    ↓
Sapphire Unitは!Enabledでwriteを捨てる
    ↓
最初のscanline
    ↓
UnitSyncがEnabledだけtrueにする
    ↓
捨てたregister stateは戻らない
    ↓
白画面／空の2D／HUD欠落
```

これは現在の白画面症状と一致する。

---

# 5. P0修正
# power／register orderingをSapphireと一致させる

## 5.1 `SetPowerCnt()`を両stateへ同時適用する

```cpp
void GPU::SetPowerCnt(u32 val) noexcept
{
    const bool engineAEnabled =
        (val & (1u << 1u)) != 0;

    const bool engineBEnabled =
        (val & (1u << 9u)) != 0;

    GPU2D_A.SetEnabled(engineAEnabled);
    GPU2D_B.SetEnabled(engineBEnabled);

#if defined(MELONPRIME_DS) \
 && defined(MELONPRIME_ENABLE_VULKAN)
    if (auto* state =
            TryGetSapphireGpu2DState())
    {
        state->UnitA.SetEnabled(
            engineAEnabled);

        state->UnitB.SetEnabled(
            engineBEnabled);
    }
#endif

    GPU3D.SetEnabled(...);
    ScreenSwap = ...;
}
```

Sapphire UnitのEnable stateを、
最初のscanlineまで遅延させない。

---

## 5.2 register writeを二重実装しない

Sapphireの`Unit::Write8/16/32()`をそのまま使用する。

adapter側でaddressごとの再実装を作らない。

```cpp
state.UnitA.Write16(addr, val);
state.UnitB.Write16(addr, val);
```

を維持する。

ただし前提として:

```text
Enabled
VCount
Power
Reset
VBlank timing
```

がSapphire Unitと一致していなければならない。

---

## 5.3 forwardingの前提をassertする

debug build:

```cpp
assert(
    state.UnitA.Enabled
    == GPU.GPU2D_A.Enabled);

assert(
    state.UnitB.Enabled
    == GPU.GPU2D_B.Enabled);
```

不一致時に黙ってwriteを捨てない。

---

# 6. P0
# `UnitSync`は完全同期ではない

現在の`SyncUnitFromGPU2D()`が同期するのは:

```text
Enabled
MasterBrightness
CaptureCnt
CaptureLatch
Display FIFO
```

だけである。

同期されない主要state:

```text
DispCnt
BGCnt[4]
BGXPos／BGYPos
BGXRef／BGYRef
BGXRefInternal／BGYRefInternal
BGRotA～D
Win0Coords／Win1Coords
WinCnt
Win0Active／Win1Active
BGMosaicSize
OBJMosaicSize
mosaic internal counters
BlendCnt
BlendAlpha
EVA／EVB／EVY
```

したがって:

```text
毎scanline UnitSyncを呼んでいる
```

ことは、完全同期を意味しない。

---

# 7. 推奨state方針

## 7.1 production方針

Sapphire本家に最も近い方針:

```text
Reset時からSapphire Unitをpersistentに保持
Power／MMIO／VBlank／FIFO／captureを
Sapphire本家と同じ順序で送る
```

これにより通常frameでfull state copyを不要にする。

---

## 7.2 full seedが必要な箇所

native stateが直接restoreされ、
MMIO replayが行われない経路だけでfull seedする。

```text
savestate load完了後
legacy state migration
backend activation時の不一致回復
```

full seedは:

```text
paused
renderLock保持
frame boundary
```

で1回だけ行う。

毎scanlineのfull `memcpy`へしない。

---

## 7.3 `SyncUnitFromGPU2D()`を分割する

```cpp
void SyncExternalGpuState(
    Unit&, const GPU2D&, GPU&);

void SeedCompleteUnitFromNative(
    Unit&, const GPU2D&, GPU&);
```

`SyncExternalGpuState()`:

```text
brightness
capture
FIFO
Enabledのassert／補助
```

`SeedCompleteUnitFromNative()`:

```text
全register
internal affine ref
window phase
mosaic phase
blend
```

を扱う。

名前からpartial syncとfull seedを区別する。

---

# 8. P0
# VBlankが二重配信されている

現在は`GPU::StartScanline(VCount=192)`で:

```cpp
ForwardVBlank(*this);
Rend->VBlank();
```

を呼ぶ。

さらにouterが`SoftRenderer`の場合、
`SoftRenderer::VBlank()`も:

```cpp
ForwardVBlank(GPU);
```

を呼ぶ。

結果:

```text
Sapphire Unit::VBlank()
    1frameに2回
```

となる。

`Unit::VBlank()`は:

```text
CaptureLatch clear
CaptureCnt bit31 clear
FIFO read pointer reset
FIFO write pointer reset
```

を行うため、二重配信はSapphire parity違反である。

---

## 8.1 修正

persistent stateのevent ownerを`GPU`へ一本化する。

```cpp
void SoftRenderer::VBlank()
{
    // Sapphire forwardingしない
}

void SoftRenderer::VBlankEnd()
{
    // Sapphire forwardingしない
}
```

`GPU`から1回だけ:

```text
VBlank
VBlankEnd
Window Check
```

を送る。

ただし`IsActiveForRendering()`または
mirror維持policyに応じて明示的にgateする。

---

# 9. P0
# renderer交換後のdangling framebuffer

## 9.1 現在の危険な流れ

```text
SoftRendererがGPU.Framebufferへ
自身のheap buffer pointerをpublish
    ↓
OpenGL／Metalへrenderer交換
    ↓
旧SoftRenderer destructor
    ↓
buffer delete[]
    ↓
GPU.Framebufferは旧pointerのまま
    ↓
persistent Sapphire rendererは生存
```

この後Sapphire callbackが動くと、
解放済みbufferを参照する可能性がある。

Software以外のrendererで落ちる場合も、
この経路を除外できない。

---

## 9.2 修正

`GPU::SetRenderer()`で旧rendererを破棄する前:

```cpp
if (Rend)
    SyncAllVRAMCaptures();

#if defined(MELONPRIME_DS) \
 && defined(MELONPRIME_ENABLE_VULKAN)
InvalidateSapphirePublication();

Framebuffer[0][0] = nullptr;
Framebuffer[0][1] = nullptr;
Framebuffer[1][0] = nullptr;
Framebuffer[1][1] = nullptr;
FrontBuffer = 0;

if (Sapphire2D)
{
    Sapphire2D->Renderer.SetFramebuffer(
        nullptr, nullptr);
}
#endif
```

新しい`SoftRenderer`かつactive Vulkanになった後だけ、
`BindSapphirePhysicalTargets()`を呼ぶ。

---

# 10. P0
# 未完成frameを`SetRenderer()`からpublishしない

現在の`GPU::SetRenderer()`末尾では、
新しいouterが`SoftRenderer`なら即座に:

```cpp
PublishCompletedSapphireFrontBuffer();
```

を呼ぶ。

この時点では:

```text
emulated frame未完了
framebufferはReset直後
VulkanFrameSerialが0の場合あり
Renderer3D install前の場合あり
```

である。

これはcompleted-frame publicationではない。

---

## 10.1 修正

`GPU::SetRenderer()`からpublicationを削除する。

publication ownerは1か所だけ:

```cpp
void GPU::FinishFrame(u32 lines)
{
    Rend->SwapBuffers();

    if (active Sapphire Vulkan 2D)
        PublishCompletedSapphireFrontBuffer();
}
```

`PublishSapphire2DFrame()`にも:

```cpp
if (!state->IsActiveForRendering())
    return false;

if (VulkanFrameSerial == 0)
    return false;
```

を追加する。

---

# 11. P1
# `LastRendererInitializationSucceeded`のstale判定

Vulkanでcurrent outerが既に`SoftRenderer`の場合:

```text
OuterAction = KeepCurrent
GPU::SetRenderer()を呼ばない
```

にもかかわらず、その直後に:

```cpp
LastRendererInitializationSucceeded()
```

を検査する。

これは過去の`SetRenderer()`結果であり、
今回のtransaction結果ではない。

修正:

```cpp
if (result.OuterAction
        == OuterRendererAction::Replace
    && !nds->GPU
            .LastRendererInitializationSucceeded())
{
    // fallback
}
```

`KeepCurrent`では:

```text
current outer type
current native resources
```

を直接検証する。

---

# 12. P1
# `SapphireVulkan2DAccess`のheap再作成は不要

現在は:

```text
GPU owns SapphireGpu2DState
GPU also owns SapphireVulkan2DAccess
```

であり、`SetRenderer()`ごとにaccess facadeを破棄／再生成する。

facadeは内部で:

```text
SapphireGpu2DState::Rendererへのreference
```

を持つだけである。

推奨:

```text
SapphireGpu2DState
 ├─ UnitA
 ├─ UnitB
 ├─ Renderer
 └─ stable AccessFacade
```

または:

```text
TryGetSapphireRenderer2D()
    ↓
underlying Rendererを直接adapter viewで返す
```

とする。

renderer交換時にfacade lifetimeを操作しない。

---

# 13. Sapphireからそのまま持ってくる範囲

## 13.1 変更せず利用する

```text
SapphireGPU2DCore/Unit.cpp
SapphireGPU2DCore/GPU2D_Soft.cpp
SapphireGPU2DCore/GPU2D_Soft.h
Sapphire compositor shader
Sapphire accumulate shader
FrameQueue
VulkanOutput
VulkanSurfacePresenterのalgorithm部分
```

今回の白画面／クラッシュ修正のために、
これらへゲーム固有例外を追加しない。

---

## 13.2 MelonPrime adapterへ限定する

```text
active backend gate
native GPU2Dとのstate bridge
renderer transaction
Qt surface
CustomHUD
frame publication
physical framebuffer binding
```

問題はSapphireのpixel algorithmではなく、
adapterの実行条件とstate orderingである。

---

# 14. parity判定

## 14.1 現在の判定

```text
shader:
    高いparity

GPU2D pixel algorithm:
    Sapphire由来

GPU2D ownership／state lifecycle:
    非parity

backend runtime isolation:
    非parity

completed frame publication:
    非parity
```

結論:

```text
「Sapphireと同じ実装」にはまだなっていない。
```

特に:

```text
SapphireではUnitがcanonical state
MelonPrimeではnative GPU2DとUnitを二重保持
```

している点が最大の差である。

---

# 15. 現在のparity testの不足

追加された:

```text
tools/test_sapphire_gpu2d_lifecycle_parity.py
```

はsource文字列を確認するstatic testである。

確認内容:

```text
KeepCurrentという文字がある
Sapphire2D ownerがある
forwarding symbolがある
timeline method名がある
```

確認できないもの:

```text
Software ROM起動時のnull dereference
VBlank二重配信
Unit Enabled不一致
失われたregister write
dangling framebuffer
frameSerial 0 publication
実frameの白画面
```

static contract testは維持してよいが、
runtime testの代替にはしない。

---

# 16. 必須runtime test

## 16.1 build capabilityとruntime selection

Vulkanを有効にbuildした同じbinaryで:

```text
Software選択
OpenGL選択
OpenGL Compute選択
Vulkan選択
```

をそれぞれROM起動する。

完了条件:

```text
Softwareはnative Rend2D_A/Bを使用
OpenGLはOpenGL pathを使用
VulkanだけSapphire 2Dを使用
```

---

## 16.2 null current renderer test

```cpp
GPU3D.SetCurrentRenderer(nullptr);
SoftRenderer.DrawScanline(0);
```

期待:

```text
native software pathへfallback
crashしない
Sapphire DrawScanline call count = 0
```

ASan／UBSanで実行する。

---

## 16.3 power ordering test

```text
GPU Reset
SetPowerCnt(0x820F)
BGCNT write
window write
blend write
first DrawScanline
```

期待:

```text
native Enabled == Sapphire Enabled
native register digest == Sapphire digest
write drop count == 0
```

---

## 16.4 VBlank count test

1 frameあたり:

```text
Sapphire UnitA VBlank = 1
Sapphire UnitB VBlank = 1
Sapphire Renderer VBlankEnd = 1
UnitA VBlankEnd = 1
UnitB VBlankEnd = 1
```

---

## 16.5 framebuffer lifetime test

```text
Software
→ OpenGL
→ Software
→ Vulkan
→ OpenGL
```

各切替後:

```text
inactive時GPU.Framebuffer == nullptr
active Vulkan時のみvalid pointer
ASan use-after-free 0
```

---

## 16.6 first publication test

Vulkan activation直後:

```text
Published2DFrame.valid == false
```

最初の`GPU::FinishFrame()`後:

```text
Published2DFrame.valid == true
frameSerial > 0
frontBuffer == completed buffer
```

---

# 17. 推奨commit分割

## S67-1

```text
Restore native Software 2D in Vulkan-capable builds
```

変更:

```text
Rend2D_A/Bを常時作成
DrawScanline／DrawSpritesをruntime branch化
```

---

## S67-2

```text
Gate Sapphire GPU2D execution to active Vulkan graphics
```

変更:

```text
IsActiveForRendering
null-safe UseStructuredVulkan2D
inactive callback禁止
```

---

## S67-3

```text
Synchronize Sapphire Unit power before GPU2D MMIO writes
```

変更:

```text
SetPowerCntでUnit SetEnabled
Enabled consistency assert
write drop診断
```

---

## S67-4

```text
Deliver Sapphire VBlank lifecycle exactly once
```

変更:

```text
GPUをevent ownerに一本化
SoftRenderer duplicate forwarding削除
```

---

## S67-5

```text
Invalidate Sapphire framebuffer bindings before renderer replacement
```

変更:

```text
GPU.Framebuffer null化
Renderer SetFramebuffer(nullptr)
publication invalidate
```

---

## S67-6

```text
Publish Sapphire frames only after GPU FinishFrame
```

変更:

```text
SetRenderer publication削除
serial 0拒否
active Vulkan gate
```

---

## S67-7

```text
Seed complete Sapphire Unit state after direct state restoration
```

変更:

```text
savestate load full seed
activation digest
partial sync／full seed分離
```

---

## S67-8

```text
Replace textual lifecycle checks with executable renderer tests
```

変更:

```text
Software ROM boot
Vulkan ROM boot
backend switch
VBlank count
power ordering
ASan／UBSan
```

---

# 18. 修正順

```text
1. S67-1
2. S67-2
3. S67-3
4. S67-4
5. S67-5
6. S67-6
7. Software／OpenGL ROM起動再検証
8. Vulkan初回ROM起動再検証
9. S67-7
10. S67-8
```

S67-1～6を先に行う。

white-screen workaroundやshader修正を先に入れない。

---

# 19. 禁止事項

```text
MELONPRIME_ENABLE_VULKANだけでruntime Sapphire pathへ入る
Software用Rend2D_A/Bを削除する
null CurrentRendererを前提にdereferenceする
Sapphire UnitがdisabledのままMMIO writeをforwardする
毎scanline全GPU2D stateをmemcpyする
VBlankをGPUとSoftRendererの両方から送る
renderer破棄後もGPU.Framebuffer pointerを残す
SetRenderer直後のzero bufferをcompleted frameとしてpublishする
白画面をshaderの色補正で隠す
Sapphire shaderへROM固有例外を追加する
static文字列testだけでruntime parity完了とする
```

---

# 20. 最終判断

今回の2症状は別々のVulkan rasterizer障害ではない。

```text
非Vulkan rendererクラッシュ:
    Sapphire 2D pathのruntime gate欠落
    null CurrentRenderer dereference

Vulkan初回白画面:
    Sapphire Unit power ordering不一致
    disabled中にGPU2D register writeを消失
    incomplete UnitSync
```

共通原因は:

```text
Sapphire GPU2Dを移植したが、
Sapphireと同じstate ownership／execution boundaryを
まだ再現できていない
```

ことである。

修正の中心はalgorithm追加ではない。

```text
Sapphire codeをそのまま維持
MelonPrime adapter側で
「いつ実行するか」
「どのstateを正規とするか」
「どの順序でeventを送るか」
を修正する
```

これが最も車輪の再発明を避ける方針である。

---

# 40. 実装進捗（S67）

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S67-1 | `3593b3ec9` | done | Restore native Rend2D_A/B in Vulkan builds |
| S67-2 | `d5f3e6ad2` | done | IsActiveForRendering runtime gate |
| S67-3 | `bfe0f3356` | done | SetPowerCnt Unit Enable sync |
| S67-4 | `7f16b40b0` | done | Single GPU VBlank owner |
| S67-5 | `81d05ecba` | done | Framebuffer binding invalidation |
| S67-6 | `2c1724a03` | done | FinishFrame-only publication |
| S67-7 | `fac349911` | done | SeedCompleteUnitFromNative on load |
| S67-8 | `536a926e8` | done | Runtime gate parity CI tests |

