# Vulkan S74 監査・修正指示書
## Sapphire Exact 実装監査
## 生成Core未使用／手編集FrameLatch残存／Parity CI実動化
## Android・Desktop差分をPlatform境界だけに限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `4c484e5fec74f4f2c2b358d1db2bb549382b6602`  
**前回監査HEAD:** `494fd0f465099c0e8ee03958d81b0e01a9965b8c`  
**差分:** 14 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire frontend commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 監査結論

S73では次が改善された。

```text
- SoftPackedFrameSnapshot ABIをSapphire 0.7.0.rc4へ戻した
- Desktop-only fieldをsidecarへ分離した
- SapphireExactをproduction defaultにした
- CurrentFrameOnly／旧repair mode headerを削除した
- Android／Desktopの許可差分を文書化した
- pinned Sapphire checkoutをCIへ追加した
- FrameLatch生成スクリプトを追加した
```

しかし、現時点では「Sapphireと同じ実装」にはなっていない。

最重要問題:

```text
P0-1:
生成したSapphireFrameLatchCoreはCMake build対象ではない。

P0-2:
productionで実際にコンパイルされるのは、
依然として手編集のSapphireVulkanFrameLatch.cppである。

P0-3:
生成Coreは現状そのままではコンパイル不能。

P0-4:
production FrameLatchにSapphire本家にない
Desktop capture provenance／black contract／stub関数が残る。

P0-5:
Parity CIはproduction FrameLatchを上流と比較していない。

P0-6:
「golden snapshot」「120-frame flicker test」は
実際のframeを生成・比較していない文字列testである。

P0-7:
VulkanOutput exact checkはVulkanOutput.cppを比較していない。

P0-8:
Desktop frame inputとlive GPU stateのatomic一致確認が不完全。
```

したがって現在の構造は:

```text
Sapphire exact生成物:
    reference artifactのみ

production:
    4,000行超の手編集FrameLatch fork

CI:
    reference artifactのhash／文字列確認
```

である。

今回の修正方針:

```text
1. Sapphireの固定版コードを実際のproduction buildへ入れる
2. 手編集FrameLatchから描画algorithmを除去する
3. Desktop差分は入力adapterとWSIだけにする
4. generator verifyをread-onlyかつcompile必須にする
5. 実行されるproduction sourceをupstreamと比較する
6. 実frameを使うgolden／flicker testへ置換する
```

新しい描画heuristicは追加しない。

---

# 1. S73で正しく改善された点

## 1.1 SoftPackedFrameSnapshot ABI

現在の`VulkanReference/VulkanOutput.h`では、
S72で追加されていた次のDesktop fieldが削除されている。

```text
sourceFrameSerial
rendererGeneration
hardwareScreenSwapLatched
renderScreenSwapAt3DLatched
topEngineLatched
bottomEngineLatched
topCapture3dSource
bottomCapture3dSource
```

現在のSnapshotはSapphire固定版と同じ主要fieldを持つ。

```cpp
u64 frameId;
int frontBufferLatched;
bool screenSwapLatched;
bool valid;
bool hasCapture3dSource;
bool captureBackedClass4Only;

packedTopPlane0;
packedTopPlane1;
packedTopControl;
packedTopLineMeta;

packedBottomPlane0;
packedBottomPlane1;
packedBottomControl;
packedBottomLineMeta;

capture3dSourceDsFrame;
captureLineUses3dMask;
captureFallbackLines;
comp4 placeholders;
top/bottom stats;
```

これは正しい。

---

## 1.2 Platform差分の方針

`S73_ANDROID_DESKTOP_ALLOWED_DIFFS.md`の原則は正しい。

許可:

```text
ANativeWindow ↔ Qt VkSurfaceKHR
Android Vulkan loader ↔ volk
filesystem／logging adapter
Desktop final CustomHUD
pointer／lifetime／generation validation
```

禁止:

```text
Snapshot field追加
temporal repair fork
partial temporal disable
VulkanOutput composition fork
capture class／screen owner／class4 threshold変更
```

この方針を実装とCIで強制する必要がある。

---

## 1.3 VulkanOutput.cppの冒頭ロジック

現在の`VulkanOutput.cpp`冒頭では、
固定Sapphireとの主な差は次に限定されている。

```text
<vulkan/vulkan.h>／VulkanDispatch
    ↔ <volk.h>

直接生成shader header
    ↔ namespaced generated shader header

256／192／stride literal
    ↔ VulkanStructuredControlAbi constants
```

冒頭のcomposition helper条件は固定Sapphireと概ね一致している。

ただし、whole-file exact parityはまだ検証されていない。

---

# 2. P0
# 生成されたSapphire Coreがproductionで使われていない

追加された生成物:

```text
src/frontend/qt_sdl/SapphireGenerated/
    SapphireFrameLatchCore.cpp
    SapphireFrameLatchCore.h
    GENERATION_MANIFEST.json
```

しかしCMakeのVulkan source listは次をコンパイルする。

```text
SapphireVulkanFrameLatch.cpp
SapphireVulkanFrameLatch.h
MelonPrimeSapphireFrameInput.cpp
MelonPrimeSapphireFrameInput.h
VulkanReference/VulkanOutput.cpp
VulkanReference/FrameQueue.cpp
...
```

次は登録されていない。

```text
SapphireGenerated/SapphireFrameLatchCore.cpp
SapphireGenerated/SapphireFrameLatchCore.h
```

結果:

```text
生成Core:
    CI参照用ファイル

production:
    旧手編集SapphireVulkanFrameLatch.cpp
```

「生成したのでSapphire exact」ではない。

実際にリンクされるobjectが上流生成物でなければ、
production parityは成立しない。

---

# 3. P0
# 生成Coreは現状コンパイル不能

現在のgeneratorはfunction blockの開始位置を:

```python
match.start()
```

としている。

pattern:

```python
\bMelonInstance::functionName\s*\(
```

はreturn typeを含まない。

そのため生成結果は次のようになる。

```cpp
SapphireFrameLatchCore::
clearLatchedSoftPackedFrameSnapshot()
{
    ...
}

SapphireFrameLatchCore::
updateVulkanTemporal3dHistoryGate()
{
    ...
}

SapphireFrameLatchCore::
latchSoftPackedFrameSnapshot(...)
{
    ...
}
```

必要な:

```cpp
void
bool
bool
```

が欠落している。

さらに生成headerにあるmemberは:

```cpp
melonDS::NDS* nds_ = nullptr;
```

だけ。

生成cppは次を参照する。

```text
lastSoftPackedFrameSnapshot
previousSoftPackedFrameSnapshot
lastValidTopScreenCapture3dDsFrame
lastValidBottomScreenCapture3dDsFrame
cachedEngineATopValid
cachedEngineABottomValid
vulkanStructuredCaptureGateFrames
vulkanTemporal3dHistoryGateFrames
vulkanTemporal3dNotReadyFrames
framesSinceLastScreenSwapToggle
wasInAlternatingMode
```

これらはheaderに宣言されていない。

また生成latchには:

```cpp
if (frame == nullptr
    || nds == nullptr
    || ...)
```

が残る。

memberは`nds_`であるため一致しない。

さらに:

```text
kPacked3dPlaceholder
kScreenshotScreenHeight
softPackedScreenUsesTemporal3dHistory
areRendererDebugBgObjLogsEnabled
GPU2D::SoftRenderer
```

などのdependency closureも完全には生成されていない。

現在これが問題化しないのは、
生成CoreをCMakeがコンパイルしていないため。

---

# 4. P0
# generatorがdependency closureを生成していない

現在の抽出対象は9関数。

```text
packedPixelHasVisibleColor
packedPixelIsOpaqueBlack
packedControlMarksProtectedBlack2D
softPackedFramesAlternate3dOwner
softPackedFrameNeedsReusablePreviousFrame
softPackedFrameUsesTemporal3dHistory
clearLatchedSoftPackedFrameSnapshot
updateVulkanTemporal3dHistoryGate
latchSoftPackedFrameSnapshot
```

しかし`latchSoftPackedFrameSnapshot()`は、
大量のhelper、constants、state membersへ依存する。

一部関数だけregex抽出しても、
standalone coreにはならない。

必要なdependency closure:

```text
- latchから到達する全free function
- constants
- debug helper declarations
- Snapshot stats helpers
- capture helper
- temporal cache state
- MelonInstance.hの関連member
- renderer access
- capture debug types
```

これを手動で補うと、
再びDesktop独自forkになる。

推奨はregexによる関数単位抽出ではなく:

```text
A. 固定upstreamの関連regionを丸ごとvendor
または
B. ASTで依存closureを抽出
```

すること。

車輪の再発明を避けるならAを優先する。

---

# 5. P0
# productionは依然として手編集FrameLatch

productionの`SapphireVulkanFrameLatch.cpp`は、
固定Sapphire由来の大部分に加えて、
Desktop独自の処理を含む。

例:

```cpp
bool packedPixelIsPresent2D(...);
bool packedLineHasPresent2D(...);
bool screenHasPresent2DContent(...);

struct SoftPackedBlackContractStats;
measureSoftPackedBlackContract(...);
```

さらに:

```cpp
bool canCarryPreviousPhysicalScreen(...)
{
    return true;
}

bool captureSourceMatchesTarget(...)
{
    return false;
}
```

というstubが存在する。

これらはSapphire固定版のplatform adapterではない。

特に:

```text
canCarryPreviousPhysicalScreen = 常にtrue
captureSourceMatchesTarget = 常にfalse
```

は、以前のDesktop provenance designを
compileさせるための無効stubになっている。

production codeに残すべきではない。

---

# 6. P0
# Sidecarがalgorithmへ入り込んでいる

文書では:

```text
DesktopSapphireFrameSidecarはvalidation metadataのみ
Sapphire algorithmへ渡さない
```

とされている。

しかしproduction latch APIは:

```cpp
latchSoftPackedFrameSnapshot(
    const SapphireFrameInput& input,
    const DesktopSapphireFrameSidecar& sidecar,
    bool useStructuredVulkan2D);
```

となっている。

Latch内部では:

```text
lastFrameSidecar_
previousFrameSidecar_
physicalTopEngine
physicalBottomEngine
topCapture3dSource
bottomCapture3dSource
```

を保持する。

さらにcapture source取得後:

```text
topOwnsCaptureを統計から推測
physical screenをtag
engineをtag
frame serial／generationをtag
```

している。

これは単なるvalidationではなく、
capture composition semanticsである。

Android／Desktopのplatform差ではない。

---

# 7. P0
# capture provenance処理が論理的に死んでいる

production latchはcapture sourceを取得すると:

```cpp
const bool topOwnsCapture =
    top VramCaptureUses3dLines
        >= bottom VramCaptureUses3dLines
    && top RegularCaptureUses3dLines
        >= bottom RegularCaptureUses3dLines;
```

として所有画面を推定する。

その後tagged sourceを作る。

しかしrepair側は:

```cpp
captureSourceMatchesTarget(...)
```

を呼ぶ。

現在の実装:

```cpp
return false;
```

したがって:

```text
tagged source生成
    ↓
match関数が必ずfalse
    ↓
repairは必ず不発
```

となる。

これはSapphire exactでも、
有効なDesktop adapterでもない。

削除する。

---

# 8. P0
# Input adapterが完全なimmutable frame inputになっていない

`SapphireFrameInput`には:

```text
capture3dSource
captureLineMask
```

fieldがある。

しかし`BuildDesktopSapphireFrameInput()`は
これらを設定しない。

一方Latchは後からlive rendererへアクセスし:

```text
GetDebugCapture3dSource()
GetDebugCaptureLineUses3dMask()
GetDebugCaptureStats()
```

を再取得する。

つまりframe inputをfreezeした後も、
別時点のlive renderer stateを読む。

これはatomic snapshotではない。

修正は二択。

推奨A:

```text
固定Sapphireと同じくLatchが同じGPU transaction内で取得
```

Desktop publicationが完全に同期しているなら、
upstreamと同じaccess形へ合わせる。

代替B:

```text
capture source／mask／statsも
BuildDesktopSapphireFrameInputでfreeze
```

その場合はLatchでlive再取得しない。

中途半端に混在させない。

---

# 9. P0
# published stateとlive stateの一致確認が不足

FrontendSessionではcomplete時に:

```cpp
const int frontBuffer =
    nds->GPU.FrontBuffer;

const bool preparedFrameScreenSwap =
    nds->GPU.GPU3D.RenderScreenSwapAt3D;
```

を取得する。

しかし`BuildDesktopSapphireFrameInput()`へ
これらを渡していない。

adapterは:

```cpp
input.frontBuffer =
    published.frontBuffer;

input.preparedFrameScreenSwap =
    published.renderScreenSwapAt3D;
```

を使用する。

現在のvalidationは:

```text
serial一致
generation一致
packed pointer存在
```

を確認するが、次を確認しない。

```text
published.frontBuffer
    == live frontBuffer

published.renderScreenSwapAt3D
    == live preparedFrameScreenSwap
```

ローカル変数`frontBuffer`と
`preparedFrameScreenSwap`は取得されるが、
実際のlatch／prepareには使われない。

publicationが1frame古い場合:

```text
3D frame serial:
    current

published packed 2D:
    previous

screen swap:
    previous
```

の組合せを通す可能性がある。

---

# 10. P0
# FrameLatch生成物のparity testがproductionを検査しない

`check_sapphire_frame_latch_parity.py`が検査するもの:

```text
manifestが存在
generated filesが存在
required function名がmanifestにある
generator --verifyが0を返す
```

検査しないもの:

```text
production CMakeが生成Coreをcompileするか
生成CoreがC++としてcompileできるか
production wrapperが生成Coreを呼ぶか
手編集algorithmが残っていないか
generated headerに必要stateがあるか
生成Coreのreturn typeが正しいか
```

このため、現在のcompile不能・未使用状態でも
parity testを通せる。

---

# 11. P0
# `--verify`がread-onlyではない

generatorの`verify()`は:

```python
core_cpp = build_core_cpp(upstream)
manifest = write_outputs(core_cpp, upstream)
```

を実行する。

つまりverify時にも:

```text
generated header
generated cpp
manifest
```

を書き換える。

その後、生成hashをstored hashと比較する。

問題:

```text
- CIのverifyがworking treeを変更する
- 差分検出前にcommit済み生成物を上書きする
- header hashを比較しない
- upstream commit SHAを直接確認しない
- compile確認しない
```

推奨:

```text
temp directoryへ生成
committed filesとbyte compare
working treeを書き換えない
```

---

# 12. P0
# VulkanOutput exact checkが.cppを比較していない

`check_sapphire_vulkan_output_exact.py`は
名前上はVulkanOutput exact checkだが、
実際に比較するのは:

```text
SoftPackedFrameSnapshot
```

のみ。

他のstructは:

```python
self.assertIn("struct Name", desktop_text)
```

で存在確認するだけ。

比較していないもの:

```text
VulkanOutput.cpp
VulkanCompositionInputs body
VulkanOutputTemporalStats body
class4判定
previous source選択
liveSourceScreenSwap
capture source選択
structured handoff判定
threshold
control bit
```

現在の`VulkanOutput.cpp`冒頭は上流と近いが、
whole-file parityは未証明。

---

# 13. P0
# `header compile` testはcompileしていない

`check_vulkan_output_header_compile.py`は
Pythonで文字列とfieldを検査する。

実際には:

```text
C++ compilerを起動しない
headerをincludeしない
objectを生成しない
linkしない
```

したがって名称が誤解を招く。

現状の生成Core compile errorも検出できない。

名前を:

```text
check_vulkan_output_header_contract.py
```

へ変更するか、
本当にcompilerを呼ぶ。

---

# 14. P0
# Golden Snapshot testがGolden testではない

`test_sapphire_vulkan_golden_snapshot_s73.py`が行うこと:

```text
Snapshot struct名がある
Desktop field名がSnapshotにない
sidecar field名がある
input adapter名がある
```

実際に行わないこと:

```text
Sapphire Androidを実行
Desktopを実行
同一savestateを使用
frame dataをcapture
packed planesを比較
controlを比較
lineMetaを比較
final outputを比較
```

これはgolden snapshot testではない。

名称をcontract testへ変更するか、
実行fixtureを追加する。

---

# 15. P0
# 120-frame flicker testがframeを生成していない

`test_sapphire_vulkan_flicker_s73.py`が行うこと:

```text
SapphireExact文字列がある
temporal gate文字列がある
CurrentFrameOnly defaultがない
診断field名がある
```

実際には:

```text
120 frameを生成しない
frame hashを取らない
period-2を検出しない
点滅pixelを比較しない
```

名称と実態が一致していない。

---

# 16. P0
# CIにclean Vulkan buildがない

`sapphire-vendor-parity.yml`は
pinned upstreamをcheckoutする点は正しい。

しかし実行するのはPython scriptだけ。

次を行わない。

```text
CMake configure
Vulkan ON build
generated core compile
Windows build
Linux build
Debug build
Release build
link
headless runtime test
```

最新HEADについてworkflow runも確認できない。

S73完了扱いは早い。

---

# 17. S74基本設計
# Source of Truthを1つにする

現在:

```text
A. pinned Sapphire source
B. generated reference Core
C. hand-edited production FrameLatch
```

の3つが存在する。

S74後:

```text
A. pinned Sapphire source
    ↓ deterministic vendor/generator
B. compiled production Sapphire Core
    ↓ thin Desktop adapter
C. FrontendSession
```

の2段にする。

手編集production algorithmを廃止する。

---

# 18. 最優先方針
# regex function extractionをやめて関連regionを丸ごとvendor

車輪の再発明を避ける最も安全な方法:

```text
Sapphireの関連コードを
そのままvendorする
```

推奨構造:

```text
src/SapphireVendor/frontend/
    MelonInstanceVulkanLatch.inc
    MelonInstanceVulkanLatchState.inc
    MelonInstanceVulkanHelpers.inc
```

または:

```text
src/frontend/qt_sdl/SapphireGenerated/
    SapphireFrameLatchCore.h
    SapphireFrameLatchCore.cpp
```

ただし生成元は:

```text
MelonInstance.cppの完全dependency closure
MelonInstance.hの完全state closure
```

とする。

許可transformのみ:

```text
MelonInstance
    → SapphireFrameLatchCore

nds
    → nds_

include path変換

Vulkan dispatch include変換

namespace wrapper
```

禁止transform:

```text
条件式変更
閾値変更
pixel判定変更
capture owner変更
screenSwap変更
temporal gate変更
cache変更
control bit変更
```

---

# 19. S74-1
# まず生成Coreをcompile対象にして現在の不備を可視化

CMakeへ一時的に追加する。

```cmake
target_sources(melonDS PRIVATE
    SapphireGenerated/
        SapphireFrameLatchCore.cpp
    SapphireGenerated/
        SapphireFrameLatchCore.h
)
```

この段階ではproductionから呼ばなくてよい。

目的:

```text
return type欠落
member欠落
helper欠落
namespace不一致
nds／nds_不一致
```

をcompilerで全て検出する。

CIでcompileが通るまで次phaseへ進めない。

---

# 20. S74-2
# generatorをdependency-closure generatorへ置換

## 20.1 関数signature

関数開始を:

```text
return typeを含む行頭
```

から抽出する。

regex修正だけで済ませない。

推奨:

```text
tree-sitter-cpp
clang AST
```

または固定sourceのmarker／line rangeを使用。

固定tagなので、
verified byte range vendorでもよい。

---

## 20.2 State closure

`MelonInstance.h`から次を生成する。

```text
lastSoftPackedFrameSnapshot
previousSoftPackedFrameSnapshot
lastValid capture buffers
resolved primary buffers
line masks
capture validity flags
regular capture resync flag
structured capture gate
temporal history gate
not-ready frames
debug log budget
Engine A cache
atypical display cache
swap toggle counters
```

手書きしない。

---

## 20.3 Helper closure

latchから呼ばれる全helperを抽出する。

generator終了時:

```text
unresolved symbol list = 0
MISSING upstream function = 0
MelonInstance:: token = 0
bare `nds` member access = 0
```

を必須とする。

---

# 21. S74-3
# verifyをread-only化

```python
def verify():
    generate_to(temp_dir)

    compare_bytes(
        temp_dir/generated.h,
        committed/generated.h)

    compare_bytes(
        temp_dir/generated.cpp,
        committed/generated.cpp)

    verify_upstream_commit()

    run_compile_check()
```

禁止:

```text
verify中にcommitted fileを書き換える
```

確認:

```bash
git diff --exit-code
```

をverify後に実行する。

---

# 22. S74-4
# production FrameLatchをthin wrapperへ変更

最終形:

```cpp
class SapphireVulkanFrameLatch
{
public:
    bool latch(
        const SapphireFrameInput& input,
        bool structured)
    {
        core_.bindNds(nds_);

        return core_
            .latchSoftPackedFrameSnapshot(
                input.frame,
                input.frontBuffer,
                input.preparedFrameScreenSwap,
                structured);
    }

private:
    SapphireFrameLatchCore core_;
};
```

実際にはinput pointer sourceをcoreへ渡す必要がある場合、
最小のsource access adapterを作る。

Wrapperに残してよいもの:

```text
generation validation
published pointer lifetime validation
diagnostic logging
Desktop error reporting
```

Wrapperに残してはいけないもの:

```text
pixel classification
capture class
screen owner
black contract
previous carry
cache repair
temporal threshold
```

---

# 23. S74-5
# 手編集algorithm bodyを削除

削除対象:

```text
SapphireVulkanFrameLatch.cpp内の
上流algorithmの複製

Desktop black contract
Desktop present2D helper
capture provenance repair
canCarryPreviousPhysicalScreen stub
captureSourceMatchesTarget stub
topOwnsCapture推定
tagged capture source
```

残す場合はdebug-onlyで、
Snapshotやcomposition inputを書き換えない。

---

# 24. S74-6
# Desktop Sidecarを純粋な診断metadataにする

推奨:

```cpp
struct DesktopSapphireFrameSidecar
{
    u64 emulatedFrameSerial;
    u64 rendererGeneration;

    int publishedFrontBuffer;
    int liveFrontBuffer;

    bool publishedScreenSwap;
    bool liveScreenSwap;

    const void* packedTopIdentity;
    const void* packedBottomIdentity;
};
```

削除:

```text
Capture3DSourceSnapshot
topCapture3dSource
bottomCapture3dSource
captureMode
sourceA
sourceB
capture pixels
```

SidecarをSapphire coreへ渡さない。

---

# 25. S74-7
# Atomic frame identityを完成させる

adapter API:

```cpp
DesktopSapphireFrameBuildResult
BuildDesktopSapphireFrameInput(
    Frame* frame,
    const SapphirePublished2DFrame&
        published,
    const Vulkan3DFrameView&
        frame3d,
    u64 activeGeneration,
    int expectedFrontBuffer,
    bool expectedScreenSwap);
```

必須validation:

```cpp
published.frontBuffer
    == expectedFrontBuffer

published.renderScreenSwapAt3D
    == expectedScreenSwap

published.emulatedFrameSerial
    == frame3d.FrameSerial

published.rendererGeneration
    == frame3d.Generation

frame.rendererGeneration
    == frame3d.Generation

frame3d.Generation
    == activeGeneration
```

0をwildcardとして扱う設計を縮小する。

production frameでは全serial／generationを必須にする。

不一致時:

```text
frame全体をreject
last completed final frameを再present
```

pixel／line補完をしない。

---

# 26. S74-8
# 同じimmutable tupleをLatchとVulkanOutputへ渡す

次を一度だけfreezeする。

```cpp
const SapphireFrameInput input =
    BuildDesktopSapphireFrameInput(...);
```

その後:

```cpp
core.latch(
    input.frame,
    input.frontBuffer,
    input.preparedFrameScreenSwap,
    ...);

output.prepareFrameForPresentation(
    input.frame,
    gpu,
    input.frontBuffer,
    input.preparedFrameScreenSwap,
    core.snapshot(),
    renderer3D);
```

同じtransaction内で
live GPU stateを再取得しない。

---

# 27. S74-9
# Capture input方針を1つに統一

現在の`SapphireFrameInput`に:

```text
capture3dSource
captureLineMask
```

があるが設定されていない。

二択:

## 推奨A: upstream access維持

```text
fieldをInputから削除
Sapphire coreが固定Sapphireと同じ時点で取得
```

## B: fully frozen input

```text
capture source
capture line mask
capture stats
structured plane pointers
```

を全てadapterでfreezeし、
coreはlive rendererを再読しない。

AとBを混在させない。

「Sapphireをそのまま使う」目的ならA。

---

# 28. S74-10
# VulkanOutput whole-file parity

比較対象:

```text
VulkanOutput.h
VulkanOutput.cpp
FrameQueue.h
FrameQueue.cpp
Vulkan compositor shaders
accumulate shader
```

allowlist transform:

```text
<vulkan/vulkan.h>
    ↔ <volk.h>

VulkanDispatch
    ↔ volk dispatch

shader include path
namespace prefix
ABI constant expression
Desktop pipeline cache adapter
```

normalized comparisonで
次をtoken／AST単位で一致させる。

```text
if conditions
thresholds
control bit masks
class4 state
screenSwap
previous source
capture source
structured handoff
temporal state transitions
```

`VulkanOutput.cpp`を比較しない
現在のexact checkerは置換する。

---

# 29. S74-11
# Actual compiler test

最低限:

```text
Ubuntu GCC／Clang:
    generated core object compile

Windows MinGW:
    full Qt Vulkan build

Linux:
    full Qt Vulkan build

Debug:
    assertions enabled

Release:
    NDEBUG
```

script名:

```text
compile_sapphire_generated_core.py
```

よりもCMake targetを推奨。

```cmake
add_library(
    sapphire_frame_latch_compile_test
    OBJECT
    SapphireGenerated/
        SapphireFrameLatchCore.cpp)
```

---

# 30. S74-12
# Real Golden Snapshot test

同じ入力fixtureを
上流coreとDesktop coreへ渡す。

fixture:

```text
packed top
packed bottom
structured top planes
structured bottom planes
capture source
capture mask
screen swap
front buffer
capture stats
previous snapshot
```

比較:

```text
packedTopPlane0
packedTopPlane1
packedTopControl
packedTopLineMeta

packedBottomPlane0
packedBottomPlane1
packedBottomControl
packedBottomLineMeta

capture3dSourceDsFrame
captureLineUses3dMask
captureFallbackLines
captureBackedClass4Only
topScreenStats
bottomScreenStats
history gate
```

binary fixtureへ記録する場合、
必ずupstream commit SHAをmetadataに持たせる。

---

# 31. S74-13
# Real 120-frame flicker test

実際に120 frameのsequenceを入力する。

各frame:

```text
raw packed hash
latched snapshot hash
VulkanOutput prepared input hash
final compositor output hash
```

period-2検出:

```cpp
if (hash[n] == hash[n - 2]
    && hash[n] != hash[n - 1])
{
    period2Count++;
}
```

静止2D区間の完了条件:

```text
unexpected period2Count = 0
```

意図したアニメーション区間は
upstream Android hash sequenceと比較する。

---

# 32. CI修正

workflow順:

```text
1. checkout MelonPrimeDS
2. checkout pinned Sapphire frontend
3. checkout pinned Sapphire core
4. verify exact commit SHA
5. generate to temp
6. byte／AST parity
7. git diff --exit-code
8. compile generated core
9. configure full Vulkan build
10. build
11. run golden fixture
12. run 120-frame flicker test
13. upload mismatch artifacts
```

現在のstatic contract testsは残してよいが、
名称を正確にする。

```text
golden_snapshot_contract
flicker_contract
header_contract
```

実行testと混同しない。

---

# 33. 推奨commit分割

## S74-1

```text
Compile the generated Sapphire FrameLatch core in CI
```

## S74-2

```text
Replace regex function extraction with the complete pinned latch dependency closure
```

## S74-3

```text
Generate Sapphire FrameLatch state from pinned MelonInstance.h
```

## S74-4

```text
Make Sapphire generator verification read-only and commit-pinned
```

## S74-5

```text
Add generated SapphireFrameLatchCore to the production Vulkan build
```

## S74-6

```text
Reduce SapphireVulkanFrameLatch to a Desktop input wrapper
```

## S74-7

```text
Remove Desktop pixel, capture and temporal algorithms from the wrapper
```

## S74-8

```text
Make DesktopSapphireFrameSidecar diagnostic-only
```

## S74-9

```text
Require atomic published/live frame identity
```

## S74-10

```text
Verify VulkanOutput and FrameQueue implementation bodies against pinned Sapphire
```

## S74-11

```text
Add real upstream-versus-Desktop golden snapshot fixtures
```

## S74-12

```text
Add real 120-frame period-two flicker detection
```

## S74-13

```text
Require clean Windows and Linux Vulkan builds in Sapphire parity CI
```

---

# 34. 実装順

```text
1. S74-1
2. 現生成Coreのcompile failureを全て確認
3. S74-2
4. S74-3
5. S74-4
6. generated core単体compile
7. S74-5
8. S74-6
9. S74-7
10. S74-8
11. S74-9
12. full Vulkan build
13. S74-10
14. S74-11
15. S74-12
16. S74-13
```

---

# 35. 削除対象一覧

```text
production SapphireVulkanFrameLatch.cpp内:

packedPixelIsPresent2D
packedLineHasPresent2D
screenHasPresent2DContent
SoftPackedBlackContractStats
measureSoftPackedBlackContract
canCarryPreviousPhysicalScreen stub
captureSourceMatchesTarget stub
topOwnsCapture heuristic
tagged capture source repair
Desktop-only capture source matching
```

上流に同名・同内容の実装があるものは、
generated core側の上流版だけを残す。

---

# 36. 維持対象一覧

```text
SoftPackedFrameSnapshot exact ABI
VulkanOutput upstream composition logic
FrameQueue upstream state machine
pinned frontend tag
pinned core commit
volk loader adaptation
Qt VkSurfaceKHR host
timeline／queue-family Desktop fix
CustomHUD final pass
physical top／bottom publication contract
```

---

# 37. 禁止事項

```text
- 新しい点滅抑制heuristicを追加する
- 手編集FrameLatchと生成Coreを両方維持する
- 生成Coreをreference-onlyのまま完了扱いする
- compileしないコードのhashだけ検査する
- source文字列testをgolden testと呼ぶ
- source文字列testを120-frame testと呼ぶ
- sidecarからcomposition ownerを決定する
- capture ownerをline countから推測する
- stubで旧algorithmを無効化して残す
- verify中に生成fileを書き換える
- VulkanOutput.hだけ比較してexactとする
- upstreamと異なるthresholdを追加する
- Android／Desktop差としてpixel semanticsを変更する
```

---

# 38. 完了条件

```text
1. production objectがgenerated Sapphire core
2. generated coreがWindows／Linuxでcompile
3. manual FrameLatchはthin wrapperのみ
4. production algorithmの上流normalized diffが0
5. Snapshot ABIがupstream exact
6. VulkanOutput.cpp全体のalgorithm差が0
7. FrameQueue algorithm差が0
8. Sidecarはdiagnostic-only
9. published/live frame identityがatomic一致
10. golden fixtureで全Snapshot field一致
11. 120-frame実行testでunexpected period-2 = 0
12. CIでfull Vulkan build成功
13. Android／Desktop差がWSI・loader・HUD境界だけ
```

---

# 39. 最終判断

S73は方針とSnapshot ABIを改善した。

しかし現在は:

```text
生成したSapphire core:
    未使用
    compile不能

production:
    手編集FrameLatch
    Desktop helper／sidecar semantics残存

parity:
    reference hash／文字列test
```

である。

したがって:

```text
Sapphireと同じ実装になった
```

とは判断できない。

車輪の再発明を避ける最短経路は:

```text
固定SapphireのFrameLatch dependency closureを
そのままproduction objectとしてcompileし、
Desktop側をthin input／WSI adapterだけにする
```

こと。

次の監査では、ファイルが存在するかではなく:

```text
どのobjectが実際にlinkされるか
そのobjectのalgorithm bodyがupstreamと一致するか
同一fixtureで同一Snapshotを出すか
```

を完了判定にする。

---

# 40. 進捗

| Phase | Commit | Status | Notes |
|-------|--------|--------|-------|
| S74-1 | `f0da47a19` | done | `sapphire_frame_latch_core` OBJECT target + compile script |
| S74-2 | `67e0777d3` | done | Line-range vendor manifest + full latch closure generator |
| S74-3 | `a5381f157` | done | `MelonInstance.h` state members in generated header |
| S74-4 | `339323e03` | done | Read-only `--verify` + byte compare |
| S74-5 | `c981df7b2` | done | `SapphireFrameLatchCore.cpp` in production `melonDS` sources |
| S74-6 | `6330c6c48` | done | Thin `SapphireVulkanFrameLatch` wrapper |
| S74-7 | `1e802d857` | done | Desktop algorithms removed from wrapper |
| S74-8 | `9db172d72` | done | Diagnostic-only `DesktopSapphireFrameSidecar` |
| S74-9 | `4a9cb9524` | done | Atomic published/live frame identity validation |
| S74-10 | `4158b526c` | done | VulkanOutput/FrameQueue composition body parity |
| S74-11 | `8d694c145` | done | Golden snapshot fixtures |
| S74-12 | `ae6303cf8` | done | 120-frame period-2 flicker fixture |
| S74-13 | `58065b990` | done | Sapphire parity CI: tests + Linux Vulkan build |
