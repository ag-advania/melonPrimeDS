# ROM選択直後クラッシュ
## Sapphire初期化契約・2D Publication・Vendor Parity監査修正指示書（S64）

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `e9529a9644cdaf319c68847ea8b0dc1c680f029b`  
**前回監査HEAD:** `ee2ce6ccef8f4c9ae439c4ed1fd8bca8b18df4c7`  
**前回監査後:** 9 commits ahead / 0 behind  
**実行バイナリ:** `melonPrimeDS v3.4.3 v65.exe`  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android` tag `0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib` commit `d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 症状

```text
ROMを選択すると即座にプロセスが終了する。
```

ログ終端:

```text
[RomBootTrace] updateConsole begin nds=0000000000000000
[RomBootTrace] BIOS loaded
[RomBootTrace] firmware loaded
[RomBootTrace] renderLock acquired
[RomBootTrace] new NDS begin

<プロセス終了>
```

存在しないログ:

```text
[RomBootTrace] new NDS complete
[RomBootTrace] initial NDS Reset begin
```

したがってクラッシュ位置は:

```text
new NDS(std::move(ndsargs), this)
```

の内部、すなわちNDS member constructor中である。

ROM CPU実行、cart install、Vulkan frame producer、presenter、CustomHUDにはまだ到達していない。

---

# 1. 結論

## 1.1 即落ちの根本原因はコード上確定

S63-4で追加された`PublishSapphire2DFrame()`が、
NDS constructor中にcurrent 3D renderer未設定のまま
Sapphire structured 2D getterを呼んでいる。

実際の呼出し列:

```text
NDS::NDS(...)
    ↓
GPU::GPU(...)
    ↓
GPU::SetRenderer(nullptr)
    ↓
GPU3D.SetCurrentRenderer(nullptr)
    ↓
SoftRendererを生成
    ↓
SoftRenderer::Reset()
    ↓
GPU::SetRenderer後処理
    ↓
SoftRenderer::SyncSapphireFramebufferBindings()
    ↓
SoftRenderer::PublishSapphire2DFrame()
    ↓
Sapphire2DRenderer->GetStructuredVulkan2DPlane(...)
    ↓
Sapphire GPU2D::SoftRenderer::UseStructuredVulkan2D()
    ↓
GPU.GPU3D.GetCurrentRenderer()
    ↓
*CurrentRenderer
```

`GPU3D::GetCurrentRenderer()`:

```cpp
Renderer3D& GetCurrentRenderer() noexcept
{
    return *CurrentRenderer;
}
```

`CurrentRenderer == nullptr`なので、
Windows release buildではnull objectに対するvirtual callとなり、
access violationで即終了する。

---

## 1.2 回帰導入commit

直接の回帰は:

```text
S63-4
6f04262ecd1c1807599c83ae775aafc2b77734ee
Add immutable Sapphire 2D frame publication
```

S63-4以前の`SyncSapphireFramebufferBindings()`は
framebuffer pointer mappingのみを行っていた。

S63-4以降:

```cpp
void SoftRenderer::SyncSapphireFramebufferBindings() noexcept
{
    ...
    PublishSapphire2DFrame();
}
```

となった。

この関数は次の複数phaseから呼ばれる。

```text
1. NDS／GPU constructor中
2. 各scanline描画後
3. FinishFrameのSwapBuffers後
4. renderer refresh
```

frame publicationを実行してよいのは原則3だけである。

---

## 1.3 Sapphire本体と同じコードであることが原因ではない

Sapphire upstreamにも:

```cpp
bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();
}
```

がある。

これはSapphire側のライフサイクル契約:

```text
structured planeを問い合わせる時点では
current rendererが必ず設定済み
```

を前提としている。

melonPrimeDS desktop adapterが:

```text
SetCurrentRenderer(nullptr)
↓
structured plane getter
```

という禁止順序を作った。

したがってSapphire algorithm本体へnull guardを追加してはいけない。

直す場所はdesktop adapterの初期化順とpublication call siteである。

---

# 2. 現在の初期化順

## 2.1 `EmuInstance::updateConsole()`

```cpp
Platform::Log(..., "[RomBootTrace] new NDS begin\n");

nds = new NDS(std::move(ndsargs), this);

Platform::Log(..., "[RomBootTrace] new NDS complete ...\n");
```

ログは`new NDS begin`で終了する。

---

## 2.2 `NDS::NDS()`

member initialization:

```cpp
GPU(*this, std::move(args.Renderer))
```

初回`NDSArgs::Renderer`は未指定なので、
`GPU::SetRenderer()`はsoftware renderer fallbackへ入る。

---

## 2.3 `GPU::SetRenderer()`

現在:

```cpp
GPU3D.SetCurrentRenderer(nullptr);

if (!good)
{
    Rend = std::make_unique<SoftRenderer>(NDS);
    Rend->Init();
    Rend->Reset();
}

SapphireVulkan2DAccess =
    std::make_unique<SapphireGPU2D::SoftRenderer>(
        softRenderer->GetSapphire2DRenderer());

softRenderer->SyncSapphireFramebufferBindings();
```

最後の`SyncSapphireFramebufferBindings()`が
S63-4以降publicationまで行う。

---

## 2.4 `PublishSapphire2DFrame()`

現在:

```cpp
if (Sapphire2DRenderer != nullptr)
{
    published.top.structuredPlane0 =
        Sapphire2DRenderer->GetStructuredVulkan2DPlane(true, 0);
    ...
}
```

ここでは:

```text
Sapphire2DRenderer != nullptr
```

しか確認していない。

必要なprecondition:

```text
GPU.GPU3D.HasCurrentRenderer()
GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata()
completed 2D frame
stable front buffer
stable screen ownership
```

を確認していない。

---

# 3. P0 即時修正

# 3.1 `Sync`と`Publish`を分離する

現在の1関数3責務:

```text
front buffer pointer mapping
physical top/bottom mapping
frame publication
```

を分離する。

## `GPU_Soft.h`

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    void SyncSapphireFramebufferBindings() noexcept;
    bool PublishSapphire2DFrame() noexcept;
    void ClearPublishedSapphire2DFrame() noexcept;
#endif
```

---

## `SyncSapphireFramebufferBindings()`

この関数はpointer mappingだけに戻す。

```cpp
void SoftRenderer::SyncSapphireFramebufferBindings() noexcept
{
    GPU.FrontBuffer = BackBuffer ^ 1;

    for (int buffer = 0; buffer < 2; ++buffer)
    {
        u32* const unitA = Framebuffer[buffer][0];
        u32* const unitB = Framebuffer[buffer][1];

        if (GPU.ScreenSwap)
        {
            GPU.Framebuffer[buffer][0] = unitA;
            GPU.Framebuffer[buffer][1] = unitB;
        }
        else
        {
            GPU.Framebuffer[buffer][0] = unitB;
            GPU.Framebuffer[buffer][1] = unitA;
        }
    }

#ifndef NDEBUG
    assert(GPU.FrontBuffer == 0 || GPU.FrontBuffer == 1);
#endif

    // PublishSapphire2DFrame()をここから呼ばない。
}
```

---

# 3.2 publicationはframe boundaryだけ

既に正しいhookがある。

```cpp
void GPU::FinishFrame(u32 lines) noexcept
{
    Rend->SwapBuffers();

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    RefreshSapphireVulkanBindings();
#endif

    ...
}
```

ここを:

```cpp
void GPU::FinishFrame(u32 lines) noexcept
{
    Rend->SwapBuffers();

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (auto* softRenderer = dynamic_cast<SoftRenderer*>(Rend.get()))
    {
        softRenderer->SyncSapphireFramebufferBindings();
        (void)softRenderer->PublishSapphire2DFrame();
    }
#endif

    TotalScanlines = lines;

    ...
}
```

とする。

原則:

```text
constructor:
  binding only
  publicationなし

scanline:
  必要ならbindingのみ
  publicationなし

FinishFrame after SwapBuffers:
  binding
  publication
```

---

# 3.3 structured getterのpreconditionをadapterで確認

Sapphire coreの`UseStructuredVulkan2D()`は変更しない。

desktop publication側:

```cpp
bool SoftRenderer::PublishSapphire2DFrame() noexcept
{
    SapphirePublished2DFrame published{};

    published.frontBuffer = GPU.FrontBuffer;
    published.hardwareScreenSwap = GPU.ScreenSwap;
    published.renderScreenSwapAt3D = GPU.GPU3D.RenderScreenSwapAt3D;
    published.emulatedFrameSerial = GPU.VulkanFrameSerial;
    published.publicationGeneration =
        GPU.Published2DFrame.publicationGeneration + 1;

    if (published.frontBuffer < 0 || published.frontBuffer > 1)
        return false;

    published.top.packed =
        GPU.Framebuffer[published.frontBuffer][0];
    published.bottom.packed =
        GPU.Framebuffer[published.frontBuffer][1];

    if (published.top.packed == nullptr
        || published.bottom.packed == nullptr)
    {
        return false;
    }

    published.top.physicalScreen = SapphirePhysicalScreen::Top;
    published.bottom.physicalScreen = SapphirePhysicalScreen::Bottom;
    published.top.engine = GPU.ScreenSwap ? 0u : 1u;
    published.bottom.engine = GPU.ScreenSwap ? 1u : 0u;

    const bool structuredReady =
        Sapphire2DRenderer != nullptr
        && GPU.GPU3D.HasCurrentRenderer()
        && GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();

    if (structuredReady)
    {
        published.top.structuredPlane0 =
            Sapphire2DRenderer->GetStructuredVulkan2DPlane(true, 0);
        published.top.structuredPlane1 =
            Sapphire2DRenderer->GetStructuredVulkan2DPlane(true, 1);
        published.top.structuredControl =
            Sapphire2DRenderer->GetStructuredVulkan2DPlane(true, 2);

        published.bottom.structuredPlane0 =
            Sapphire2DRenderer->GetStructuredVulkan2DPlane(false, 0);
        published.bottom.structuredPlane1 =
            Sapphire2DRenderer->GetStructuredVulkan2DPlane(false, 1);
        published.bottom.structuredControl =
            Sapphire2DRenderer->GetStructuredVulkan2DPlane(false, 2);

        if (published.top.structuredPlane0 == nullptr
            || published.top.structuredPlane1 == nullptr
            || published.top.structuredControl == nullptr
            || published.bottom.structuredPlane0 == nullptr
            || published.bottom.structuredPlane1 == nullptr
            || published.bottom.structuredControl == nullptr)
        {
            return false;
        }
    }

    GPU.Published2DFrame = published;
    return true;
}
```

注意:

```text
HasCurrentRenderer()の後だけGetCurrentRenderer()を呼ぶ。
```

---

# 3.4 constructorではpublicationしない

`GPU::SetRenderer()`:

```cpp
if (auto* softRenderer = dynamic_cast<SoftRenderer*>(Rend.get()))
{
    SapphireVulkan2DAccess =
        std::make_unique<SapphireGPU2D::SoftRenderer>(
            softRenderer->GetSapphire2DRenderer());

    softRenderer->SyncSapphireFramebufferBindings();

    // PublishSapphire2DFrame()は呼ばない。
}
```

これだけで今回の即落ちは止まる。

---

# 3.5 renderer交換前にpublicationをclear

`SapphirePublished2DFrame`は名前に反してpixel snapshotではない。

保持しているもの:

```cpp
const u32* packed;
const u32* structuredPlane0;
const u32* structuredPlane1;
const u32* structuredControl;
```

rendererを破棄するとdangling pointerになる。

`GPU::SetRenderer()`冒頭:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    Published2DFrame = {};
    SapphireVulkan2DAccess.reset();
    GPU3D.SetCurrentRenderer(nullptr);
#endif
```

旧`Rend`を破棄する前にclearする。

backend switch、console rebuild、renderer init fallbackでも同じ。

---

# 3.6 reset時にもclear

`GPU::Reset()`:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    Published2DFrame = {};
#endif
```

`SoftRenderer::Reset()`:

```cpp
Sapphire2DRenderer->ClearStructuredVulkan2DState();
SapphireUnitA.Reset();
SapphireUnitB.Reset();
```

の後も、completed frameが生成されるまではpublication invalidとする。

---

# 4. scanline publicationを禁止する理由

現在は`DrawScanline()`の最後に:

```cpp
SyncSapphireFramebufferBindings();
```

を呼ぶため、S63-4以降は各scanlineでpublicationしている。

問題:

```text
raw packed:
  FrontBuffer＝前回completed bufferを指す場合がある

structured plane:
  現在描画中frameのlineが順次更新される

screen ownership:
  現在のGPU.ScreenSwap

3D ownership:
  VCount 215でlatchされたRenderScreenSwapAt3D

frame serial:
  別のframe boundaryで更新
```

同一publication object内に:

```text
previous raw frame
current partial structured frame
current screen swap
latched 3D swap
```

が混在し得る。

今回のクラッシュをguardだけで止めても、
2D黒透過／背景全面表示の原因を残す。

したがってpublicationは必ず:

```text
SwapBuffers後
completed 2D frame
structured planes completed
screen ownership確定
```

で一度だけ行う。

---

# 5. 「immutable」の再定義

現在の`SapphirePublished2DFrame`はimmutable frameではなく:

```text
mutable bufferへのpointer descriptor
```

である。

最低限必要なlifetime contract:

```cpp
struct SapphirePublished2DFrame
{
    ...
    u64 publicationGeneration;
    u64 rendererGeneration;
    u64 frameSerial;
    bool valid;
};
```

consumer条件:

```text
valid == true
rendererGeneration一致
frameSerial一致
publicationGenerationが期待値以上
pointer ownerが生存
次frameの書込み開始前にlatch完了
```

より安全な構造:

```cpp
struct SapphirePublished2DFrameView
{
    std::span<const u32> topPacked;
    std::span<const u32> bottomPacked;
    std::span<const u32> topStructuredPlane0;
    ...
    std::shared_ptr<const Sapphire2DBufferLease> lease;
};
```

ただし新しいlease systemを即座に発明する必要はない。

現在のproducerがframe completion直後に同期的にsnapshot copyするなら:

```text
frame-boundary publication
renderer swap時clear
次frame開始前にcopy完了
```

をassertで固定すればよい。

---

# 6. Sapphireと同じ実装か

## 6.1 exact一致を確認できるもの

manifest上、次は`exact_upstream`。

```text
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
```

特にS63-6はcompositor compute shaderをSapphireへ戻している。

これは維持する。

---

## 6.2 一致を確認できていないもの

manifest上`normalized_upstream`:

```text
FrameQueue.h／cpp
VulkanOutput.h／cpp
VulkanSurfacePresenter.h／cpp
GPU2D_Soft.h／cpp
```

しかしcheckerに制御フローバグがあるため、
localとupstreamのnormalized contentは比較されていない。

---

# 7. Vendor parity checkerのバグ

現在:

```python
if mode in {"exact_upstream", "normalized_upstream"}:
    ...
    if mode == "exact_upstream" and upstream_hash != local_hash:
        ...

elif mode == "normalized_upstream" and verify_upstream:
    ...
```

`normalized_upstream`は最初の`if`へ入るので、
後ろの`elif`には絶対に到達しない。

現在のnormalized modeが確認しているもの:

```text
Sapphire checkout側ファイルのhashがmanifestと一致する
```

確認していないもの:

```text
local adapted fileとSapphire fileが同じalgorithmか
```

したがってCIがpassしても:

```text
Sapphireと同じ
```

とは言えない。

---

# 8. parity checker修正

```python
def check_entry(entry: dict, verify_upstream: bool) -> list[str]:
    errors = []

    local_path = REPO_ROOT / entry["local_path"]
    if not local_path.is_file():
        return [f"missing local file: {entry['local_path']}"]

    local_hash = sha256_file(local_path)
    mode = entry.get("parity_mode")

    if mode == "local_baseline":
        if local_hash != entry["local_sha256"]:
            errors.append(...)

    elif mode == "exact_upstream":
        upstream_path = require_upstream(entry, verify_upstream, errors)
        if upstream_path is not None:
            upstream_hash = sha256_file(upstream_path)
            verify_manifest_upstream_hash(...)
            if local_hash != upstream_hash:
                errors.append(...)

    elif mode == "normalized_upstream":
        upstream_path = require_upstream(entry, verify_upstream, errors)
        if upstream_path is not None:
            upstream_hash = sha256_file(upstream_path)
            verify_manifest_upstream_hash(...)

            local_norm = normalize_vendor_text(
                local_path.read_text(encoding="utf-8"))
            upstream_norm = normalize_vendor_text(
                upstream_path.read_text(encoding="utf-8"))

            if local_norm != upstream_norm:
                errors.append(...)

    else:
        errors.append(f"unknown parity mode: {mode}")

    return errors
```

必須unit test:

```text
1. exact fileを1文字変更 → fail
2. normalized algorithm条件を変更 → fail
3.許可adapter blockだけ追加 → pass
4. unknown parity_mode → fail
5. upstream checkoutなし＋--verify-upstream → fail
```

---

# 9. 現在のnormalized方式は修正後そのままでは通らない

`normalize_vendor_text()`が除去するもの:

```text
Source header
MELONPRIME_DESKTOP_ADAPTER_BEGIN／END block
```

しかし現在のvendored fileには:

```text
namespace SapphireGPU2DCore追加
include path変更
VulkanDesktopCompat追加
debug helper adaptation
desktop dispatch adaptation
```

がmarker外に存在する。

checker分岐を直すと、多数のnormalized mismatchが出る可能性が高い。

それはchecker故障ではなく、
今まで隠れていた実差分である。

manifest hashを更新してpassさせてはいけない。

---

# 10. 車輪の再発明を避けるvendor構成

## 10.1 推奨構成

```text
src/SapphireVendor/upstream/
    GPU2D.h
    GPU2D.cpp
    GPU2D_Soft.h
    GPU2D_Soft.cpp
    FrameQueue.h
    FrameQueue.cpp
    VulkanOutput.h
    VulkanOutput.cpp
    VulkanSurfacePresenter.h
    VulkanSurfacePresenter.cpp
    shaders...

src/SapphireAdapter/
    SapphireGpuStateAdapter.cpp
    SapphirePublished2DFrame.cpp
    SapphireDesktopSurfaceAdapter.cpp
    SapphireDesktopOverlayHook.cpp
```

`upstream/`はbyte-exactで保存する。

algorithmを直接編集しない。

---

## 10.2 namespace変更が必要な場合

手編集しない。

固定commitからdeterministic generatorで生成する。

```text
tools/vendor_sapphire.py
```

許可transform:

```text
namespace prefix
include path
dispatch include
build guard
source provenance header
```

禁止transform:

```text
if条件
pixel判定
capture mode
threshold
wait条件
frame queue state transition
resource lifetime
```

CI:

```text
Sapphire固定SHA checkout
↓
generator実行
↓
生成物とrepository committed fileをbyte compare
```

これなら「同じalgorithm」を機械保証できる。

---

# 11. S63-5はdependency closureではない

S63-5の実変更:

```text
SoftRenderer::VBlank()追加
SapphireUnitA.VBlank()
SapphireUnitB.VBlank()

UnitSyncから:
BGMosaicY
BGMosaicYMax
OBJMosaicY
の上書きを削除
```

一方、現在も各scanlineで手動copy:

```text
DispCnt再合成
BGCnt
BG position
affine refs
window coords
window active
mosaic size
blend
brightness
capture
display FIFO
```

Sapphire `Unit`が持つ:

```text
Write8／16／32
CheckWindows
UpdateMosaicCounters
OBJMosaicYCount
OBJMosaicYMax
CaptureLatch lifecycle
VBlank lifecycle
```

を正式なstate ownerとして使っていない。

したがってcommit名の:

```text
Vendor Sapphire GPU2D dependency closure
```

は実装内容より強すぎる。

---

# 12. GPU2DをSapphireへ寄せる実装順

即落ち修正と同時に大規模state置換をしない。

## Phase A: boot回復

```text
Sync／Publish分離
null current renderer参照排除
renderer swap時pointer clear
frame-boundary publication
```

## Phase B: parity可視化

```text
checker分岐修正
vendor coverage追加
local／upstream real diffを出力
```

## Phase C: Unit lifecycle移植

Sapphireからそのまま持つ範囲:

```text
Unit class
Unit::Reset
Unit::Write8／16／32
Unit::VBlank
Unit::CheckWindows
Unit::UpdateMosaicCounters
capture／FIFO state
GPU2D SoftRenderer
```

melonPrime adapterの責務:

```text
現行melonDS register writeをSapphire Unitへforward
VRAM access bridge
physical top/bottom publication
Vulkan frame latch
```

毎scanline全field copyは段階的に廃止する。

---

# 13. `UnitSync`の最終形

第一選択:

```text
register write forwarding
```

例:

```cpp
void MelonPrimeSapphireGpu2DAdapter::Write16(
    u32 engine,
    u32 addr,
    u16 value)
{
    UnitFor(engine).Write16(addr, value);
}
```

event forwarding:

```text
VBlank
VBlankEnd
scanline/window check
display FIFO sampling
capture latch
```

一時的に残すfield syncは:

```text
明示的にSapphire側へ存在しないhost stateだけ
```

とする。

`memcpy`で内部counterを毎scanline上書きしない。

---

# 14. 追加のlifetime問題

## 14.1 `SapphireVulkan2DAccess`

これはownerへのreferenceを保持するfacade。

```cpp
SapphireGPU2DCore::GPU2D::SoftRenderer& Owner;
```

旧`Rend`を破棄するとdangling referenceになる。

renderer交換順:

```text
Published2DFrame clear
SapphireVulkan2DAccess.reset
旧Rend破棄
新Rend生成
新facade生成
```

に固定する。

---

## 14.2 `Published2DFrame`

旧renderer pointerを保持しないよう:

```cpp
void GPU::InvalidateSapphirePublication() noexcept
{
    Published2DFrame = {};
}
```

を作る。

呼出し:

```text
GPU::SetRenderer冒頭
GPU::Reset
GPU::Stop
console teardown
backend switch begin
renderer init failure
device loss
```

---

# 15. diagnostics

今回のクラッシュ修正確認用に一時ログを追加する。

```text
[RomBootTrace] GPU SetRenderer begin
[RomBootTrace] GPU3D current cleared
[RomBootTrace] SoftRenderer constructed
[RomBootTrace] SoftRenderer reset
[RomBootTrace] Sapphire facade bound
[RomBootTrace] framebuffer bindings synchronized
[RomBootTrace] GPU SetRenderer complete
```

publication:

```text
[Sapphire2DPublish]
reason=finishFrame
frontBuffer=
hardwareSwap=
render3DSwap=
frameSerial=
rendererPresent=
structured=
generation=
```

禁止reason:

```text
constructor
scanline
setRenderer-before-current3D
```

debug assert:

```cpp
assert(GPU.GPU3D.HasCurrentRenderer()
    || published.top.structuredControl == nullptr);

assert(GPU.GPU3D.HasCurrentRenderer()
    || published.bottom.structuredControl == nullptr);
```

---

# 16. 必須テスト

## 16.1 constructor

```text
アプリ起動
ROMなし画面
NDS ROM初回起動
別ROM起動
ROM再起動
console rebuild
DSi mode
```

完了条件:

```text
new NDS begin
new NDS complete
initial NDS Reset begin
initial NDS Reset complete
cart installation complete
```

---

## 16.2 renderer

```text
Software
OpenGL Classic
OpenGL Compute
Vulkan
Software → Vulkan
Vulkan → Software
Vulkan → Vulkan再生成
renderer init failure fallback
```

完了条件:

```text
current renderer null期間中にstructured getter 0回
dangling facade 0
dangling Published2DFrame pointer 0
```

---

## 16.3 Vulkan frame

```text
300 frames以上
screen swap
match transition
display capture
CustomHUD
fullscreen
```

完了条件:

```text
publication 1回／completed frame
scanline publication 0
raw／structured frameSerial一致
raw／structured physical owner一致
```

---

## 16.4 sanitizers

最低1つ:

```text
Windows MinGW ASan build
clang-cl ASan
MSVC AddressSanitizer
```

確認対象:

```text
null dereference
use-after-free
dangling facade
buffer lifetime
```

---

# 17. Commit分割

## S64-1

```text
Stop publishing Sapphire 2D state during renderer construction
```

変更:

```text
SyncからPublish呼出し削除
constructorはbinding only
HasCurrentRenderer guard
```

## S64-2

```text
Publish Sapphire 2D frame only after SwapBuffers
```

変更:

```text
FinishFrame publication
scanline publication削除
generation／valid contract
```

## S64-3

```text
Invalidate Sapphire 2D pointers across renderer replacement
```

変更:

```text
Published2DFrame clear
facade reset order
reset／stop／switch invalidation
```

## S64-4

```text
Fix normalized Sapphire parity verification
```

変更:

```text
unreachable elif修正
unknown mode fail
checker unit tests
```

## S64-5

```text
Generate Sapphire vendor sources from pinned upstream
```

変更:

```text
byte-exact upstream snapshot
deterministic namespace/include transform
committed generated output comparison
```

## S64-6

```text
Complete Sapphire GPU2D lifecycle adapter
```

変更:

```text
register write forwarding
window／mosaic／capture event forwarding
partial UnitSync縮小
```

---

# 18. 禁止事項

```text
Sapphire GPU2D::UseStructuredVulkan2D()へ場当たりnull guard
GPU3D::GetCurrentRenderer()をdummy renderer返却へ変更
constructor中に空structured planeを偽装
ROM別例外
catch(...)でクラッシュを隠す
PublishSapphire2DFrameをscanlineごとに継続
dangling pointerをgenerationだけで隠す
manifest local hash更新だけでparity pass
normalized checkerの比較を再びskip
S63全体を一括rollback
Vulkan 3D rasterizerへの無関係な変更
```

---

# 19. 最終修正方針

今回の即落ちは:

```text
Sapphire codeをそのまま使ったこと
```

ではなく:

```text
Sapphire codeのpreconditionをdesktop adapterが破ったこと
```

である。

正しい修正:

```text
Sapphire algorithm:
  変更しない

desktop adapter:
  current renderer確立前にgetterを呼ばない
  bindingとpublicationを分離
  frame boundaryで一度だけpublish
  renderer交換時にpointerをinvalidate
```

また現在のparity CIはnormalized local contentを比較していないため、
「Sapphireと同じ実装になった」とはまだ判定できない。

次の順で進めること。

```text
1. S64-1～3でROM即落ちを修正
2. ROM起動を実機確認
3. S64-4でparity checkerを本当に動かす
4. 出た実差分を一覧化
5. algorithmはSapphireから再vendor
6. desktop固有差分だけadapterへ残す
```

この順序ならVulkan 3Dの正常部分を壊さず、
Sapphireの実装を最大限そのまま利用できる。

---

# 20. 実装進捗（S64）

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S64-1 | `668fb2e4c` | done | Sync/Publish分離、HasCurrentRenderer guard |
| S64-2 | — | in progress | FinishFrame publication、valid contract |
| S64-3 | — | pending | renderer swap時pointer invalidate |
| S64-4 | — | pending | parity checker分岐修正 |
| S64-5 | — | pending | vendor generator |
| S64-6 | — | pending | GPU2D lifecycle adapter |

