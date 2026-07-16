# Vulkan S79 監査・フェーズ別修正指示書
## ROM Open直後 ACCESS_VIOLATION
## DrawSprites(Unit B)完了後のHBlank runtime gate crash
## Sapphire GPU2D Event Block Exact化
## Android／Desktop差分をPlatform境界へ限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `b7037ef6e6c89c185135c26470d4a2366efc0661`  
**HEADコミット:** `S78-1: Refresh reproduction baseline after Unit-B scanline fix`  
**前回監査HEAD:** `edbdda85434ba3c1d9790cfc5ed15445cc3f1a76`  
**差分:** 8 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 今回の結論

添付された最新ログにより、前回のクラッシュ位置判定は更新する必要がある。

以前の判定:

```text
Unit B DrawScanline(0)内部でクラッシュ
```

最新ログで確認できた実際の到達点:

```text
Unit A DrawScanline(0) complete
Unit B DrawScanline(0) complete
Unit A DrawSprites(1) complete
Unit B DrawSprites(1) complete
ACCESS_VIOLATION
```

したがって、Unit Bのscanline描画本体は今回のrunでは完了している。

最新ソースの`GPU::StartHBlank()`では、
`after UnitB sprites`の直後に次が実行される。

```cpp
if (UsesSapphireGpu2DPath() && line <= 2)
{
    Log(
        LogLevel::Debug,
        "[FirstGpuLine] before CheckDMAs HBlank ...");
}

NDS.CheckDMAs(0, 0x02);
```

添付ログには:

```text
[FirstGpu2D] after UnitB sprites line=1
```

までは存在するが、

```text
[FirstGpuLine] before CheckDMAs HBlank
```

が存在しない。

現時点で最も狭いクラッシュ候補区間は:

```text
DrawSprites(Unit B) return
    ↓
UsesSapphireGpu2DPath()の再評価
    ↓
before CheckDMAs log
```

である。

固定Sapphireの`StartHBlank()`には、
このruntime gate再評価は存在しない。

固定Sapphireは描画後に直接:

```cpp
NDS.CheckDMAs(0, 0x02);
```

へ進む。

したがって最優先修正は:

```text
HBlank中にSapphire pathを何度も再判定しない
```

こと。

---

# 1. 添付artifactの整合性問題

## 1.1 build commitがunknown

実行ログ:

```text
[BuildIdentity] commit=unknown
```

この状態では、クラッシュアドレスを
監査HEADや特定commitへ対応付けられない。

## 1.2 report timestampが一致しない

実行ログが生成したartifact:

```text
melonPrimeDS-unknown-1784177116.dmp
melonPrimeDS-unknown-1784177116.crash.txt
```

添付されたreport:

```text
melonPrimeDS-unknown-1784177102.crash.txt
```

つまり今回提供されたconsole logとcrash reportは、
同一runのartifactではない。

添付reportの:

```text
exception address=0x00007ff6ff21f901
```

を今回のconsole traceへ直接対応させてはならない。

## 1.3 symbolization不能

reportには:

```text
NeAACDecAudioSpecificConfig+0x7D8EA4
```

という表示があるが、これは正しいfunction symbolとは限らない。

commit、binary hash、module base、symbolsが無いため、
現段階のstack表示は根本原因の証拠として使えない。

---

# 2. 最新HEADで改善された点

S78実装により、次は前回より改善された。

```text
- GPU::StartHBlankからSapphire Renderer2Dを直接呼ぶ
- Unit A/B DrawScanline順序をSapphireと同じA→Bへ変更
- Unit A/B DrawSprites順序をA→Bへ変更
- SetPowerCnt後にAssignFramebuffersを実行
- AssignFramebuffersがPowerControl9 bit15を参照
- outer SoftRendererからSapphire draw bridgeを削減
- Unit B DrawScanline内部の細分化traceを追加
- Windows Debug、ASan/UBSan用workflowを追加
- cold-start crash reproduction artifact bundleを追加
```

したがって、S78以前の:

```text
Unit B DrawScanlineのどこかで即死
```

という問題は少なくとも今回のlogでは通過している。

---

# 3. P0
# 固定Sapphireに存在しないHBlank runtime gate

現在の`StartHBlank()`では、
同一HBlank内で`UsesSapphireGpu2DPath()`を複数回呼ぶ。

```text
1. DrawScanline前
2. DrawSprites前
3. CheckDMAs trace前
4. trace budget処理前
```

`UsesSapphireGpu2DPath()`:

```cpp
return Sapphire2D != nullptr
    && Sapphire2D->IsActiveForRendering(*this)
    && GPU2D_Renderer != nullptr;
```

`IsActiveForRendering()`はさらに:

```cpp
return SapphireRenderingActive
    && gpu.GPU3D.HasCurrentRenderer()
    && ActiveSapphireRendererGeneration
        == gpu.GPU3D.GetCurrentRendererGeneration()
    && gpu.GPU3D.GetCurrentRenderer()
        .UsesStructured2DMetadata();
```

を実行する。

つまり各scanline／HBlankで:

```text
Sapphire activation state
GPU3D renderer pointer
GPU3D generation
virtual method call
```

を何度も横断する。

固定Sapphireにはこの設計は無い。

固定SapphireではGPU2D Rendererはcanonical memberであり、
HBlank中に3D rendererを照会して2D rendererを有効判定しない。

---

# 4. このgateが危険な理由

## 4.1 cross-subsystem virtual call

GPU2D HBlank処理中に:

```text
GPU2D
    → SapphireGpu2DState
    → GPU3D current renderer
    → virtual UsesStructured2DMetadata()
```

という依存が入る。

GPU2Dのline processingが、
Desktop Vulkan rendererのlifetimeへ直接依存している。

これはAndroid／DesktopのWSI差ではない。

GPU timing semanticsへのDesktop固有依存。

## 4.2 同一HBlank内で判定が変化可能

最初の判定がtrueでも、
描画処理後の再判定で:

```text
renderer generation
current renderer pointer
activation flag
```

のどれかが壊れていればクラッシュする。

本来、一つのHBlank transaction中に
backend pathが切り替わってはならない。

## 4.3 hidden corruptionの検出点になる

実際の破壊が`DrawSprites()`内部だった場合でも、
最初に破損pointerをdereferenceする場所が
`UsesSapphireGpu2DPath()`となる可能性がある。

この場合も、post-draw gateは不要であり、
固定Sapphireとの差分である。

---

# 5. 即時hotfix
# 1 HBlankにつきpathを1回だけ確定する

最小修正:

```cpp
void GPU::StartHBlank(u32 line) noexcept
{
    const bool useSapphire2D =
        UsesSapphireGpu2DPath();

    ...

    if (VCount < 192)
    {
        if (line < 192)
        {
            if (useSapphire2D)
            {
                GPU2D_Renderer->DrawScanline(
                    line, &GPU2D_A);
                GPU2D_Renderer->DrawScanline(
                    line, &GPU2D_B);
            }
            else
            {
                Rend->DrawScanline(line);
            }
        }

        if (line < 191)
        {
            if (useSapphire2D)
            {
                GPU2D_Renderer->DrawSprites(
                    line + 1, &GPU2D_A);
                GPU2D_Renderer->DrawSprites(
                    line + 1, &GPU2D_B);
            }
            else
            {
                Rend->DrawSprites(line + 1);
            }
        }

        if (useSapphire2D && line <= 2)
            logBeforeDma();

        NDS.CheckDMAs(0, 0x02);
    }

    ...
}
```

重要:

```text
DrawScanline／DrawSprites後に
UsesSapphireGpu2DPath()を再度呼ばない
```

これでクラッシュが消えた場合、
post-draw runtime gateが直接triggerだったと判断できる。

これでクラッシュ位置がDMAへ移る場合、
DrawSprites以前のmemory corruptionが残っている。

---

# 6. 最終修正
# HBlankからruntime backend gateを除去する

hotfixのlocal boolも最終形ではない。

固定Sapphireに近づける最終構造:

```text
renderer transaction開始
    ↓
frame boundaryでGPU2D pathを確定
    ↓
1 frame中は固定
    ↓
StartHBlankはcanonical rendererを直接実行
```

例:

```cpp
enum class GPU2DExecutionPath : u8
{
    LegacyOuterRenderer,
    SapphireCanonical
};

GPU2DExecutionPath ActiveGPU2DPath;
```

更新可能な場所:

```text
- renderer transaction commit
- NDS Reset前
- frame boundary
```

更新禁止:

```text
- HBlank途中
- DrawScanline途中
- DrawSprites途中
- DMA callback途中
```

より推奨する最終形:

```text
MelonPrime Vulkan buildでは
Sapphire GPU2D Rendererをcanonical rendererとして常時使用
```

3D backendがSoftware／OpenGL／Vulkanであっても、
2D Unit／Renderer lifecycleは同じSapphire実装を使用する。

AndroidとDesktopの差はpresentation以降へ限定する。

---

# 7. P0
# lifecycle differential testが実装差を検出しない

現在の:

```text
test_sapphire_gpu2d_lifecycle_differential_s78.py
```

は、localとupstreamの双方に次の文字列があることだけを検査する。

```text
DrawScanline Unit A
DrawScanline Unit B
DrawSprites Unit A
DrawSprites Unit B
```

検査しないもの:

```text
- 余分なUsesSapphireGpu2DPath()
- 余分なGPU3D virtual call
- call sequence間に別処理が挟まる
- DMA前のruntime gate
- scheduling差
- IRQ前後の差
- VBlank条件差
```

そのため、現在のcrash候補差分が存在してもtestはgreenになる。

---

# 8. parity testの修正

## 8.1 GPU2D event blockだけを正規化比較

melonDS本体のversion差を考慮し、
`StartHBlank()`全体を古いSapphireへ戻さない。

比較対象はGPU2D event block。

```text
DrawScanline A
DrawScanline B
DrawSprites A
DrawSprites B
CheckDMAs HBlank
```

正規化後の期待sequence:

```text
GPU2D_Renderer->DrawScanline(line,&GPU2D_A)
GPU2D_Renderer->DrawScanline(line,&GPU2D_B)
GPU2D_Renderer->DrawSprites(line+1,&GPU2D_A)
GPU2D_Renderer->DrawSprites(line+1,&GPU2D_B)
NDS.CheckDMAs(0,0x02)
```

allowlist:

```text
- first-frame trace
- braces
- comments
```

禁止差分:

```text
- UsesSapphireGpu2DPath
- GPU3D renderer lookup
- dynamic_cast
- generation comparison
- backend switch
- alternate frame selection
```

## 8.2 ASTまたはgenerated include

推奨:

```text
tools/generate_sapphire_gpu2d_event_block.py
```

固定SapphireからGPU2D event blockを生成し、
Desktop側はtrace hookだけをmarkerへ注入する。

例:

```cpp
#include "SapphireGenerated/SapphireGpu2DHBlankCore.inc"
```

これにより同等コードの再実装を避ける。

---

# 9. P0
# crash reproduction testが修正後に必ず失敗する

現在の:

```text
test_sapphire_vulkan_cold_start_reproduction_s78.py
```

は成功条件として:

```text
processがACCESS_VIOLATIONで終了すること
first RunFrame completeが無いこと
```

を要求する。

workflowでは通常のcold-start成功testと
crash再現testの両方を必須実行している。

現在:

```text
cold-start成功test
    → crashするためfail

crash reproduction test
    → crashするためpass
```

修正後:

```text
cold-start成功test
    → pass

crash reproduction test
    → crashしないためfail
```

つまり現在のworkflowは、
クラッシュを修正してもgreenにならない。

---

# 10. reproduction testの正しい扱い

## Phase A: 未修正中

crash reproductionは:

```text
workflow_dispatch
manual diagnostic
continue-on-error
```

に限定。

必須branch protection jobに入れない。

## Phase B: 修正commit後

testを反転する。

新名称:

```text
test_sapphire_vulkan_cold_start_regression_s79.py
```

成功条件:

```text
exit code = 0
after UnitB spritesがある
before CheckDMAsがある
after CheckDMAsがある
first RunFrame completeがある
queuePushがある
surfacePresent=1
splashHidden=1
```

baseline crash artifactは:

```text
tools/fixtures/crash-baselines/s78/
```

へデータとして保存し、実行testにはしない。

---

# 11. P0
# current baseline checkpointが最新到達点を固定していない

現在の`BASELINE_CHECKPOINTS`は:

```text
before UnitB sprites line=1
```

まで。

添付ログで確認済みの:

```text
after UnitB sprites line=1
```

を要求していない。

したがって、以前の位置へregressionしても
baseline testが通る可能性がある。

未修正baselineを残す期間は、
最低でも次を追加。

```text
[FirstGpu2D] after UnitB sprites line=1
```

ただし最終的にはcrash期待test自体を必須CIから外す。

---

# 12. P0
# build identityを必須化

## BuildIdentity必須field

```text
git commit SHA
branch
dirty state
build configuration
compiler
linker
build timestamp
Sapphire frontend ref
Sapphire core ref
```

`commit=unknown`の場合:

```text
cold-start CIをfail
crash artifactを正式監査対象にしない
```

## CMake

configure時に:

```text
git rev-parse HEAD
git status --porcelain
```

を実行し、generated headerへ埋め込む。

CI archiveにも同じ値を保存。

---

# 13. P0
# crash artifactをrun IDで結合する

process開始時にUUIDまたはmonotonic run IDを生成。

```text
runId=...
```

同じIDを:

```text
console log
minidump filename
crash report filename
artifact metadata
test result
screenshot
binary metadata
```

へ入れる。

例:

```text
melonPrimeDS-b7037ef6-run-42.dmp
melonPrimeDS-b7037ef6-run-42.crash.txt
melonPrimeDS-b7037ef6-run-42.log
```

testは同じrun IDのartifactだけを収集する。

timestamp近似で結合しない。

---

# 14. P0
# symbolizationをCIで完了させる

crash reportへ追加。

```text
module base
exception RVA
binary SHA-256
symbol file SHA-256
thread ID
registers
stack memory
```

Windows Debug job:

```text
1. Debug binary build
2. symbols保存
3. cold-start実行
4. minidump取得
5. symbolization
6. symbolized stackをartifact化
```

最低完了条件:

```text
#00 function
#00 file:line
#01 function
#01 file:line
```

現在の`NeAACDecAudioSpecificConfig+...`表示は
完了条件を満たさない。

---

# 15. 追加trace
# post-sprite gateを直接挟む

hotfix前に次を追加すると、
triggerを一回で確認できる。

```cpp
logRaw(
    "[FirstGpuLine] after sprites "
    "Sapphire2D=%p GPU2DRenderer=%p\n",
    Sapphire2D.get(),
    GPU2D_Renderer.get());

logRaw(
    "[FirstGpuLine] before path recheck\n");

const bool postSpritePath =
    UsesSapphireGpu2DPath();

logRaw(
    "[FirstGpuLine] after path recheck result=%d\n",
    postSpritePath ? 1 : 0);
```

注意:

最初のpointer logではvirtual functionを呼ばない。

さらに:

```text
before NDS.CheckDMAs
after NDS.CheckDMAs
before IRQ
after IRQ
before ScheduleEvent
after ScheduleEvent
```

を追加。

これにより次を分離。

```text
A. UsesSapphireGpu2DPath crash
B. HBlank DMA crash
C. IRQ crash
D. event scheduling crash
```

---

# 16. 特殊register ownershipの残課題

最新実装はVulkan active時に:

```text
Capture/FIFO/Brightness read/write
    → Sapphire Unit
```

へroutingする。

ただしlegacy GPU fieldも残っている。

```text
GPU::CaptureCnt
GPU::DispFIFO
GPU::MasterBrightnessA/B
```

さらにsavestateでもlegacy fieldを保存する。

危険:

```text
UsesSapphireGpu2DPathがfalseへ変化
    ↓
同一frame中にregister ownerがUnitからGPUへ切替
```

runtime gateをframe途中で再評価しないことが
この問題の即時防止にもなる。

最終的には:

```text
canonical Unit ownerへ一本化
```

する。

---

# 17. SoftRenderer transient state

Sapphire SoftRenderer constructorは現在:

```cpp
SoftRenderer::SoftRenderer(GPU& gpu)
    : Renderer2D(),
      GPU(gpu)
{
}
```

次には明示初期値がない。

```text
BGOBJLine
_3DLine
WindowMask
OBJLine
OBJWindow
NumSprites
CurBGXMosaicTable
CurUnit
Framebuffer
```

今回のlogでは:

```text
DrawScanline Unit A/B
DrawSprites Unit A/B
```

が完了しているため、
現在の直接fault地点とは断定しない。

ただしMemorySanitizerで確認する。

未初期化が検出された場合のみ、
upstream-compatible portability patchとして初期化する。

Desktop限定workaroundにしない。

---

# 18. sanitizer workflowの問題

現在のmatrix:

```text
asan-ubsan
msan
```

しかしcold-start testは:

```yaml
if: matrix.sanitizer == 'asan-ubsan'
```

のため、MSan jobではbuildするだけで実行しない。

MSanの目的は未初期化検出なので、
実行しなければ意味がない。

対応:

```text
- MSan instrument済み依存を用意して実行
または
- MSan jobを未完了として明示
```

build成功だけをMSan greenとしない。

---

# 19. フェーズ別修正指示

---

## Phase 0 — artifact identity修正

### 目的

console log、binary、dump、report、commitを
一意に対応付ける。

### 作業

```text
- BuildIdentity commit unknownを禁止
- binary SHA-256埋込み
- run ID生成
- dump/report/logに同じrun ID
- module baseとRVAをreportへ追加
```

### 完了条件

```text
1 run = 1 commit = 1 binary = 1 dump/report pair
```

### 推奨コミット

```text
S79-1: Make Vulkan crash artifacts commit-addressable and run-paired
```

---

## Phase 1 — post-sprite crash区間確定

### 目的

UsesSapphire gate、DMA、IRQ、ScheduleEventを分離。

### 作業

```text
after sprites raw pointer log
before/after path recheck
before/after CheckDMAs
before/after IRQ
before/after ScheduleEvent
```

### 完了条件

最後に成功した命令区間を1つへ限定。

### 推奨コミット

```text
S79-2: Trace the post-sprite HBlank gate and DMA boundary
```

---

## Phase 2 — HBlank path判定を一回へ固定

### 目的

同一HBlank内のbackend再評価を除去。

### 作業

```cpp
const bool useSapphire2D =
    UsesSapphireGpu2DPath();
```

を関数冒頭で1回だけ評価。

以降はlocalを使用。

### 完了条件

```text
StartHBlank内のUsesSapphireGpu2DPath呼出し = 1
post-draw呼出し = 0
```

### 推奨コミット

```text
S79-3: Latch the GPU2D execution path once per HBlank
```

---

## Phase 3 — Sapphire event block exact化

### 目的

GPU2D描画blockを固定Sapphireから直接生成。

### 作業

```text
generate_sapphire_gpu2d_event_block.py
SapphireGpu2DHBlankCore.inc
```

比較対象:

```text
DrawScanline A/B
DrawSprites A/B
CheckDMAs
```

Desktop traceは明示hookのみ。

### 完了条件

```text
normalized GPU2D event block diff = 0
```

### 推奨コミット

```text
S79-4: Generate the GPU2D HBlank event block from pinned Sapphire
```

---

## Phase 4 — runtime Sapphire gateをframe boundaryへ移動

### 目的

HBlankからGPU3D renderer lifetime依存を除去。

### 作業

```text
- ActiveGPU2DPathをframe boundaryで確定
- renderer transaction中に更新
- HBlank/scanline中のgeneration・virtual callを削除
```

### 完了条件

```text
UsesSapphireGpu2DPath not called from:
    StartHBlank
    StartScanline
    DisplayFIFO
    DrawScanline
    DrawSprites
```

### 推奨コミット

```text
S79-5: Move Sapphire GPU2D activation decisions to renderer transaction boundaries
```

---

## Phase 5 — SapphireGpu2DStateをrendering semanticsから除去

### 目的

固定Sapphireにないmutable activation objectを縮小。

### 作業

`SapphireGpu2DState`を次へ限定。

```text
publication generation
debug status
```

GPU2D描画可否を決めない。

最終的には削除を検討。

### 完了条件

```text
GPU2D event loop does not dereference SapphireGpu2DState
```

### 推奨コミット

```text
S79-6: Remove Desktop Sapphire activation state from GPU2D timing semantics
```

---

## Phase 6 — register ownership一本化

### 目的

Unit/GPU legacy stateのruntime切替を防ぐ。

### 作業

```text
CaptureCnt
FIFO
MasterBrightness
```

をcanonical Unitへ一本化。

savestateもUnit stateだけを保存。

### 完了条件

```text
同一registerのownerが1つ
backend pathでownerが変わらない
```

### 推奨コミット

```text
S79-7: Complete canonical Sapphire ownership of 2D special registers
```

---

## Phase 7 — crash testをregression testへ反転

### 目的

修正後もCIをgreenにできる構造へ変更。

### 作業

```text
- crash expected testをrequired jobから削除
- baselineはfixture dataとして保存
- cold-start success testをrequired化
- after UnitB sprites checkpoint追加
- before/after DMA checkpoint追加
```

### 完了条件

```text
crashしないことがCI成功条件
```

### 推奨コミット

```text
S79-8: Replace the expected-crash baseline with a cold-start regression gate
```

---

## Phase 8 — symbolized Debug・sanitizer

### 目的

hidden corruptionが残る場合に正確なfaultを取得。

### 作業

```text
Windows Debug symbolization
ASan/UBSan実行
MSan実行または未完了明示
```

### 完了条件

```text
unknown stack frame = 0
sanitizer error = 0
```

### 推奨コミット

```text
S79-9: Symbolize Vulkan cold-start crashes and execute sanitizer matrices
```

---

## Phase 9 — cold-start green後の画像監査

### 目的

クラッシュを解消してから2D correctnessへ戻る。

### 検査

```text
black protection
top/bottom isolation
flicker
capture
1x/2x/4x
fullscreen
CustomHUD
```

### 前提

```text
first RunFrame complete
queuePush
PresentedGameFrame
splashHidden
```

が全buildでgreen。

### 推奨コミット

```text
S79-10: Resume Sapphire GPU2D golden validation after cold-start is stable
```

---

# 20. 推奨実装順

```text
1. S79-1
2. S79-2
3. 最新binaryで再実行
4. S79-3
5. cold-start再実行
6. S79-4
7. S79-5
8. S79-6
9. S79-7
10. S79-8
11. S79-9
12. 全matrix cold-start green
13. S79-10
```

---

# 21. 禁止事項

```text
- Unit B描画を再びskipする
- first frameだけSoftwareへfallback
- ACCESS_VIOLATIONをcatchして継続
- post-draw gateを残したままnull checkだけ追加
- GPU3D renderer virtual callをHBlank中に増やす
- crash expected testをbranch protection必須に残す
- commit unknownのdumpを正式なsymbolization結果とする
- timestampが違うlog/reportを同一runとして扱う
- Sapphire event blockを見ながら独自再実装する
- modern melonDS timing全体を古いSapphireへ無条件rollbackする
- cold-start green前に画像補正heuristicを追加する
```

---

# 22. 完了条件

## Crash

```text
ACCESS_VIOLATION = 0
first RunFrame complete
```

## HBlank

```text
DrawScanline A complete
DrawScanline B complete
DrawSprites A complete
DrawSprites B complete
CheckDMAs complete
IRQ complete
ScheduleEvent complete
```

## Sapphire parity

```text
GPU2D event block normalized diff = 0
HBlank runtime backend recheck = 0
GPU2D event loopからGPU3D virtual call = 0
```

## Artifact

```text
commit unknown = 0
run ID mismatch = 0
unsymbolized #00 = 0
```

## CI

```text
Windows Release cold-start green
Windows Debug cold-start green
Linux Debug cold-start green
ASan/UBSan green
MSan実行済みまたは未完了明示
expected-crash required test = 0
```

## Platform差

```text
Android/Desktop差:
    WSI
    Vulkan loader
    queue family
    resource lifetime
    window lifecycle
    HUD
    logging/minidump

GPU2D timing semantics差:
    0
```

---

# 23. 最終判断

最新ログにより、
Unit Bのscanline本体は今回の直接fault地点ではなくなった。

現在の最有力triggerは:

```text
DrawSprites(Unit B)完了後に行われる
Desktop独自UsesSapphireGpu2DPath再評価
```

である。

これは固定Sapphireに存在しない。

さらに現在のparity testは、
呼出し文字列の存在だけを確認するため、
この余分なruntime gateを検出しない。

最短の安全修正:

```text
1 HBlankにつきpathを一回だけ確定
```

根本修正:

```text
GPU2D event blockを固定Sapphireから生成し、
backend activation判断をframe boundaryへ移す
```

ことである。

AndroidかDesktopかの違いを、
GPU2D event timingへ持ち込んではならない。

---

# 24. 進捗

| Phase | 状態 | コミット | 備考 |
|-------|------|----------|------|
| S79-1 | 完了 | 50d4649c4 | run ID、git identity、crash artifact pairing |
| S79-2 | 完了 | ea461f08b | post-sprite gate/DMA/IRQ/ScheduleEvent trace |
| S79-3 | 完了 | acace9d63 | HBlank path latch once per entry |
| S79-4 | 完了 | ce85fefa6 | generated Sapphire GPU2D HBlank event block |
| S79-5 | 完了 | (this commit) | ActiveGPU2DPath at renderer transaction boundaries |
| S79-6 | 完了 | (this commit) | GPU2D timing no longer uses SapphireGpu2DState gate |
| S79-7 | 完了 | 15bfebb26 | canonical Unit ownership for special registers |
| S79-8 | 完了 | (this commit) | cold-start regression gate replaces expected-crash CI |
| S79-9 | 未着手 | | |
| S79-10 | 未着手 | | cold-start green 待ち |

