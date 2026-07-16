# Vulkan S75 Sapphire Exact 再監査・修正指示書
## FrameLatch本体一致後の残課題
## FrameQueue分離／VulkanOutput差分最小化／実Golden・実120-frame試験
## Android・Desktop差分をPlatform境界へ限定

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `d489c6678a4ea7b7539dc7da05fbdd21b197fc3e`  
**前回監査HEAD:** `4c484e5fec74f4f2c2b358d1db2bb549382b6602`  
**差分:** 16 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire frontend commit:** `2c10e59d7209d354e90d9ef4228330bac3f6e794`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 最終結論

S74で前回の最大問題は大きく改善された。

```text
- Sapphire FrameLatch dependency closureを固定commitから生成
- 生成Coreをproduction targetへ追加
- 手編集FrameLatchを約70行のwrapperへ縮小
- Snapshot／temporal cache stateを上流headerから生成
- generator verifyをread-only化
- published／live frame identityを強化
- Desktop sidecarからcapture ownership semanticsを削除
```

したがって、**FrameLatch algorithm本体については、現在かなり高い確度でSapphire 0.7.0.rc4と同じ**と判断できる。

ただし、Vulkan 2D pipeline全体を:

```text
Sapphireと同じ実装
Android／Desktop差はplatformだけ
```

と認定するには、まだ未達である。

現在の正確な評価:

```text
FrameLatch:
    Sapphire exactに近い
    productionで実使用される

SoftPackedFrameSnapshot:
    Sapphire exact

Desktop input validation:
    改善済み

FrameQueue:
    Sapphire state machineへDesktop独自ownership stateを直接混入

VulkanOutput:
    重要関数をparity比較から丸ごと除外

Golden test:
    実frameではなくfield名contract

120-frame flicker test:
    実hashではなく同一placeholder文字列120行

CI:
    Linuxでgenerated OBJECTだけbuild
    production melonDS full buildではない
    Windows jobなし

Production object identity:
    audited OBJECT targetとmelonDSへ直接compileされるobjectが別
```

結論:

```text
FrameLatch exact:
    合格に近い

Vulkan 2D pipeline exact:
    未合格

S74完了表:
    実装済みという意味では概ね正しいが、
    実Golden／実flicker／Windows+Linux full buildの完了表現は不正確
```

S75では新しい描画heuristicを追加しない。

必要なのは:

```text
- exact coreを1回だけcompileしproductionへlink
- Sapphire FrameQueue selection coreとDesktop lifetime管理を分離
- VulkanOutputのcomposition algorithmを比較対象へ戻す
- 実データfixtureでAndroid／Desktopを比較
- production binaryをWindows／Linuxでbuild
```

である。

---

# 1. S74で解消された問題

## 1.1 生成Coreがproductionへ入った

現在のCMakeでは:

```cmake
target_sources(melonDS PRIVATE
    SapphireVulkanFrameLatch.cpp
    SapphireGenerated/SapphireFrameLatchCore.cpp
    ...
)
```

となっている。

前回の:

```text
生成Coreはreference-only
productionは手編集FrameLatch
```

という問題は解消された。

---

## 1.2 Wrapperがthin化された

現在の`SapphireVulkanFrameLatch.cpp`は約70行である。

主な責務:

```text
- core clear delegation
- temporal gate debug mode
- input validity確認
- sidecar保持
- core latch delegation
```

pixel分類、black contract、capture owner推定、previous-frame repairなどの独自algorithmはwrapperから削除された。

これは正しい。

---

## 1.3 Generated Coreは固定Sapphire由来

generatorは次を固定している。

```json
{
  "upstreamTag": "0.7.0.rc4",
  "upstreamFrontendCommit":
    "2c10e59d7209d354e90d9ef4228330bac3f6e794",
  "cppRegions": [
    [37, 852],
    [4737, 8056]
  ],
  "headerStateMembers": [236, 268]
}
```

変換も概ね次へ限定される。

```text
MelonInstance::
    → SapphireFrameLatchCore::

nds
    → nds_

Android GPU2D dynamic_cast
    → TryGetSapphireRenderer2D()

unique_ptr framebuffer .get()
    → Desktop raw pointer

GPU2D debug type namespace
    → SapphireGPU2D namespace
```

composition条件、threshold、control bit、cache判定をgeneratorが変更していない点は良い。

---

## 1.4 State closureも上流由来

現在のgenerated headerには次が含まれる。

```text
last／previous SoftPackedFrameSnapshot
capture 3D buffers
resolved primary buffers
line masks
Engine A caches
structured capture gate
temporal history gate
not-ready counter
screen swap cadence state
```

これらは固定Sapphireの`MelonInstance.h`と一致している。

---

## 1.5 Frame identity validationが強化された

`BuildDesktopSapphireFrameInput()`はproductionで常に次を検査する。

```text
frame != null
published.valid
frame3d.Valid
frame serial != 0
generation != 0
front buffer range
published front == live front
published screen swap == live screen swap
published serial == frame3d serial
published generation == frame3d generation
frame generation == frame3d generation
frame3d generation == active generation
packed pointers != null
```

S73時点のwildcard判定は大幅に減った。

また、LatchとVulkanOutputは同じbuild resultの:

```text
frontBuffer
preparedFrameScreenSwap
```

を使用している。

これは正しい。

---

# 2. P0
# Audited OBJECTとproduction objectが別物

現在のCMakeは同じsourceを2回compileする。

```cmake
target_sources(melonDS PRIVATE
    SapphireGenerated/SapphireFrameLatchCore.cpp
)

add_library(sapphire_frame_latch_core OBJECT
    SapphireGenerated/SapphireFrameLatchCore.cpp
)
```

しかし:

```cmake
$<TARGET_OBJECTS:sapphire_frame_latch_core>
```

を`melonDS`へ入れていない。

したがって:

```text
CIがcompile確認するobject:
    sapphire_frame_latch_core OBJECT

実際にmelonDSへlinkされるobject:
    melonDS targetが別途compileしたobject
```

となる。

さらにcompile definitionも異なる。

production `melonDS`:

```text
MELONPRIME_ENABLE_VULKAN=1
VK_NO_PROTOTYPES=1
VK_USE_PLATFORM_WIN32_KHR
VK_USE_PLATFORM_METAL_EXT
VK_USE_PLATFORM_XCB_KHR
VK_USE_PLATFORM_XLIB_KHR
...
```

OBJECT target:

```text
MELONPRIME_DS
MELONPRIME_ENABLE_VULKAN=1
```

つまりOBJECT単体build成功はproduction objectの成功を証明しない。

## 修正

生成Coreを1回だけcompileする。

推奨:

```cmake
add_library(sapphire_frame_latch_core OBJECT
    SapphireGenerated/SapphireFrameLatchCore.cpp
)

target_compile_definitions(
    sapphire_frame_latch_core PRIVATE
    MELONPRIME_DS
    MELONPRIME_ENABLE_VULKAN=1
    VK_NO_PROTOTYPES=1
)

target_sources(
    melonDS PRIVATE
    $<TARGET_OBJECTS:sapphire_frame_latch_core>
)
```

またはSTATIC library化する。

```cmake
add_library(sapphire_frame_latch_core STATIC ...)
target_link_libraries(melonDS PRIVATE sapphire_frame_latch_core)
```

削除:

```cmake
target_sources(melonDS PRIVATE
    SapphireGenerated/SapphireFrameLatchCore.cpp)
```

完了条件:

```text
generated Coreのobject fileがbuild treeに1つだけ
CIでcompileしたobjectとproductionへlinkするobjectが同一
```

---

# 3. P0
# Linux full buildではなくOBJECT targetだけ

workflow名とcommit messageは:

```text
clean Windows and Linux Vulkan builds
```

を主張している。

しかし実際のworkflowはUbuntuのみ。

```yaml
vulkan-linux-build:
  runs-on: ubuntu-latest
```

実際にbuildするtarget:

```yaml
cmake --build ... --target sapphire_frame_latch_core
```

`melonDS`をbuildしていない。

したがって検査されないもの:

```text
SapphireVulkanFrameLatch wrapper
FrontendSession integration
VulkanOutput link
FrameQueue link
Qt presenter
CustomHUD
shader generated headers
duplicate symbol
missing symbol
production compile definitions
final executable link
```

Windows jobも存在しない。

## 修正

最低限:

```yaml
- name: Build production binary
  run: cmake --build build/sapphire-parity-linux --target melonDS -j 2
```

Windowsは既存の安定しているWindows build workflowを再利用する。

新しくdependency installationを再発明しない。

推奨:

```text
既存build-windows.ymlの
toolchain／vcpkg／Qt／volk設定をreusable workflow化
```

Sapphire parity workflowから`workflow_call`する。

完了条件:

```text
Linux Release melonDS link成功
Linux Debug melonDS link成功
Windows MinGW Release melonDS.exe link成功
```

---

# 4. P0
# Linux jobのdependency不足

現在インストールするもの:

```text
ninja-build
cmake
g++
qt6-base-dev
libarchive-dev
libsdl2-dev
libenet-dev
zlib1g-dev
```

しかしCMakeは次を要求する。

```text
Qt6 Core
Qt6 Gui
Qt6 Widgets
Qt6 Network
Qt6 Multimedia
Qt6 OpenGL
Qt6 OpenGLWidgets
Qt6 Svg

libzstd
faad2
volk

Linux X11/Xi
EGL
Wayland optional dependencies
```

configureが成功する保証がない。

Sapphire parity専用にdependency一覧を二重管理すると、
既存build workflowと乖離する。

## 修正

```text
Sapphire parity独自build environmentを廃止
既存Linux build jobのsetupをreusable化して使用
```

どうしても独自jobを維持する場合は、
既存workflowからpackage listを生成／共有する。

---

# 5. P0
# 120-frame fixtureがplaceholder

現在のfixture:

```text
static2d_golden_frame0_sha256
static2d_golden_frame0_sha256
...
```

同じ文字列が120行ある。

これはSHA-256ではない。

testは:

```python
hash[n] == hash[n - 2]
and hash[n] != hash[n - 1]
```

を数える。

全行が同一なので、常に:

```text
period2_count = 0
```

になる。

renderer、FrameLatch、VulkanOutputを一度も実行しない。

したがって:

```text
120-frame flicker test
```

ではなく:

```text
placeholder text format test
```

である。

## 修正

fixtureを実際の出力から生成する。

1 frameごとに最低限次をhashする。

```text
latched packedTopPlane0
latched packedTopPlane1
latched packedTopControl
latched packedTopLineMeta

latched packedBottomPlane0
latched packedBottomPlane1
latched packedBottomControl
latched packedBottomLineMeta

prepared capture3dSource
final top composed RGBA
final bottom composed RGBA
```

hash format:

```text
64桁lowercase hex
```

fixture metadata:

```json
{
  "upstreamCommit": "...",
  "desktopCommit": "...",
  "scene": "...",
  "frameCount": 120,
  "scale": 1,
  "backend": "Vulkan Graphics",
  "sourceArtifactSha256": "...",
  "perFrameFields": [...]
}
```

testで必ず検査する。

```python
assert re.fullmatch(r"[0-9a-f]{64}", hash)
assert source_artifact.exists()
assert source_artifact_sha256 == metadata value
```

静止sceneでは全frame同一でもよい。

ただしhashは実binary outputから生成しなければならない。

さらにアニメーションsceneを追加し:

```text
hashのunique数 > 1
unexpected period-2 = 0
```

も検査する。

---

# 6. P0
# Golden fixtureが実データを持たない

現在のgolden metadataはfield名一覧だけ。

test内容:

```text
- tag／commitが正しい
- compareFieldsが10個以上
- generated C++にfield名が存在
- field名一覧のhashが固定値
```

比較していないもの:

```text
input pixels
output pixels
line metadata値
capture mask値
screen stats値
Android出力
Desktop出力
```

したがって実Golden testではない。

## 修正

以下の二つを用意する。

### 6.1 Synthetic unit fixture

ROMを使わず、
Sapphire coreへ決定的なpacked／structured入力を与える。

fixture例:

```text
opaque black 2D
transparent pixel
structured 3D slot
structured 2D above
regular capture
VRAM capture
class4
screen swap alternation
partial capture lines
```

Android Sapphire coreとDesktop generated coreへ同じ入力を与え、
全Snapshot fieldをbyte compareする。

### 6.2 Integration fixture

ライセンス上問題のないhomebrew／synthetic NDS sceneを使用する。

比較:

```text
120 consecutive frame snapshots
final composed image
previous-source selection
FrameQueue selected frame IDs
```

---

# 7. P0
# VulkanOutput exact checkerが重要関数を丸ごと除外

現在のallowlistには:

```text
VulkanOutput::prepareFrameForPresentation
VulkanOutput::dispatchCompositor
VulkanOutput::createCompositorResources
VulkanOutput::createAccumulateResources
VulkanOutput::submitFrameCommand
...
```

が含まれる。

`prepareFrameForPresentation()`は:

```text
capture source selection
previous frame selection
class4 state
screen owner
structured handoff
accumulator選択
replay判定
```

を実行する核心関数。

この関数全体を比較対象外にしたまま:

```text
VulkanOutput exact
```

とは言えない。

## 現在確認できるDesktop差

Desktop版はmapped packed buffer上書き前に:

```cpp
if (frame->renderTimelineValue != 0
    && !waitForFrame(frame, UINT64_MAX))
{
    return false;
}
```

を追加している。

これはDesktop Vulkan lifetime上、必要である可能性が高い。

しかし、この1つのplatform差を許可するために
関数全体をallowlistへ入れてはいけない。

## 修正

platform hookへ抽出する。

```cpp
bool WaitBeforePackedBufferOverwrite(
    Frame* frame,
    FrameResource& resource);
```

Android:

```cpp
return true;
```

Desktop:

```cpp
return frame->renderTimelineValue == 0
    || waitForFrame(frame, UINT64_MAX);
```

そのうえで`prepareFrameForPresentation()`本体を
固定Sapphireと同じにする。

同様に:

```text
resource allocation
queue-family ownership
command submission
WSI synchronization
```

だけをplatform adapterへ分離する。

---

# 8. P0
# Function parserが重要関数を検出しない

現在のparity checker regex:

```regex
(?:bool|void|u32|int|std::...)\s+Function(...)
```

検出できない例:

```text
Frame* FrameQueue::getRenderFrame
Frame* FrameQueue::getPresentFrame
Frame* FrameQueue::getPresentCandidate
Frame* FrameQueue::getReusablePreviousFrame
constructors
destructors
operator
template
複雑なreturn type
```

またfunction名をdictionary keyにするため、
overloadは最後の1つで上書きされる。

さらにDesktopだけに存在する追加関数をrejectしない。

## 修正

regex parserを廃止する。

推奨順:

```text
1. clang AST dump
2. tree-sitter-cpp
3. 固定region byte normalization
```

Sapphireをそのまま使う目的なら、
最も単純なのは固定region vendorである。

```text
VulkanOutput algorithm region
FrameQueue selection region
```

をそのまま生成し、
platform hookだけ外から注入する。

---

# 9. P0
# FrameQueueがSapphire exactではない

現在のDesktop FrameQueueには上流にないものがある。

```text
FrameQueueState
    Free
    Rendering
    Ready
    AcquiredForPresentation
    Previous
    HistoryReferenced

historyReferences
presentationReferences
rendererGeneration
surfaceGeneration
active generation validation
state transition validation
presentation completion synchronization
history reference synchronization
```

これらはDesktop Vulkan lifetime保護として
必要である可能性がある。

しかし現在はSapphireのqueue selection algorithmへ
直接混ぜられている。

例:

Sapphire:

```cpp
Frame* frame = presentQueue.back();
presentQueue.pop_back();
return frame;
```

Desktop:

```cpp
presentQueue内から
historyReferences == 0
&& presentationReferences == 0
のframeを探索

stateをReady→Renderingへ遷移
generation mismatchを事前破棄
```

これは単なるVk handle置換ではない。

次を変える可能性がある。

```text
選ばれるframe
dropされるframe
previous reuse
queue ordering
backlog挙動
点滅周期
```

---

# 10. FrameQueue修正方針
# Selection coreとlifetime guardを分離

推奨構造:

```text
SapphireFrameQueueCore
    固定Sapphireからそのままvendor
    queue ordering
    policy
    previous reuse
    drop／defer semantics

DesktopFrameLifetimeTracker
    Vk timeline completion
    renderer generation
    surface generation
    history reference
    presentation reference
```

Core API例:

```cpp
struct FrameQueueSelection
{
    Frame* selected;
    std::vector<Frame*> dropped;
    bool reusedPrevious;
};
```

Desktop trackerはCoreへ:

```text
eligible frame set
```

だけを提供する。

ただしCoreのorderingは変えない。

より安全な方法:

```text
Sapphire Coreが選んだframeがlifetime上未完了なら
queue選択を改変せずpresentationをdefer
```

別のframeへすり替えない。

---

# 11. FrameQueue differential test

同じevent sequenceを:

```text
A. 固定Sapphire FrameQueue
B. Desktop wrapper
```

へ入力する。

比較:

```text
returned frameId
previous reuse有無
drop cause
presentQueue order
backlog depth
defer result
```

lifetime metadataが全て完了状態のfixtureでは、
AとBが完全一致しなければならない。

lifetime未完了fixtureでは:

```text
Desktopはdeferできる
別frameを選択してはならない
```

を原則にする。

---

# 12. P1
# Inputに未使用のpointer sourceが残る

`SapphireFrameInput`は次を持つ。

```text
packedTop
packedBottom
structuredTopPlane0/1/control
structuredBottomPlane0/1/control
```

adapterはこれらを設定する。

しかしgenerated exact coreはSapphire本家と同じく:

```text
nds_->GPU.Framebuffer
TryGetSapphireRenderer2D()
```

をlive参照する。

つまりInput pointerはcoreへ渡されない。

Sidecarにはpacked pointer identityが保存されるが、
wrapperはpointer equalityを検査しない。

## 修正

Sapphire exact live accessを維持する。

そのうえでadapterにlive pointerを渡し:

```cpp
published.top.packed
    == nds.GPU.Framebuffer[frontBuffer][0]

published.bottom.packed
    == nds.GPU.Framebuffer[frontBuffer][1]
```

をproductionで検査する。

structured pointerも同様。

検査後、Coreへ渡さないfieldは
`SapphireFrameInput`から削除してよい。

重複したsource-of-truthを残さない。

---

# 13. P1
# Generation manifestをverifyしていない

read-only verifyは:

```text
generated H
generated CPP
```

をtemp生成物とbyte compareする。

しかし:

```text
GENERATION_MANIFEST.json
```

を比較しない。

そのためmanifestだけが古い／改変された場合、
verifyは検出しない。

## 修正

temp directoryへmanifestも生成し:

```text
H
CPP
GENERATION_MANIFEST.json
```

の3点をbyte compareする。

manifestへ追加:

```text
upstream cpp region SHA-256
upstream header state region SHA-256
transform script SHA-256
vendor manifest SHA-256
```

---

# 14. P1
# compile helperがsilent skipする

`compile_sapphire_generated_core.py`は:

```text
ローカルMinGW cmakeがない
またはbuild treeがない
```

場合に:

```text
skipped
return 0
```

となる。

CIで実行してもcompile保証にならない。

現在workflowではこのscript自体を実行していない。

## 修正

このscriptを削除するか、modeを分ける。

```text
--required
    environment不足なら失敗

--optional-local
    local convenience用
```

CIはCMake targetを直接buildする。

---

# 15. P1
# Workflow path filterがintegration fileを取りこぼす

現在のpath filterに不足する主要file:

```text
src/frontend/qt_sdl/
    MelonPrimeVulkanFrontendSession.*
    MelonPrimeSapphirePipelineMode.h
    SapphireVulkanFramePipeline.*
    VulkanDesktopCompat.h
    MelonPrimeDesktopVulkanPresenter.*
    MelonPrimeVulkanSurfaceHost.*

src/
    GPU.cpp
    GPU.h
    GPU_Soft.*
    GPU2D.*
    SapphirePublished2DFrame.*
    MelonPrimeSapphireGpu2DAdapter.*
    MelonPrimeSapphireGpu2DState.*
```

これらを変更してもparity workflowが起動しない可能性がある。

## 修正

細かい列挙より、関連directory単位を優先する。

```yaml
- 'src/frontend/qt_sdl/**Vulkan**'
- 'src/frontend/qt_sdl/**Sapphire**'
- 'src/SapphireGPU2DCore/**'
- 'src/GPU*'
- 'tools/*sapphire*'
- 'tools/fixtures/**'
```

workflow自身もtrigger対象にする。

```yaml
- '.github/workflows/sapphire-vendor-parity.yml'
```

---

# 16. P1
# CI成功を確認できていない

最新HEADに対して、
GitHub connectorからcombined statusおよびPR workflow runは確認できなかった。

これは必ずしもpush workflowが実行されていないことを意味しないが、
少なくとも監査時点で成功証跡を取得できない。

完了表を更新する条件:

```text
実commit SHAに紐づくsuccessful run URL
Windows／Linux各job success
artifactにbinary／fixture diff report
```

を残す。

---

# 17. S75実装方針

最終構造:

```text
Pinned Sapphire source
    │
    ├─ Generated SapphireFrameLatchCore
    │      productionで唯一のcompiled object
    │
    ├─ Generated／vendored VulkanOutput algorithm core
    │
    └─ Generated／vendored FrameQueue selection core

Desktop adapters
    ├─ volk dispatch
    ├─ Qt VkSurfaceKHR
    ├─ queue-family ownership
    ├─ timeline／fence lifetime
    ├─ generation validation
    └─ CustomHUD final pass
```

Desktop adapterは:

```text
何を描画するか
どのprevious sourceを選ぶか
どのscreen ownerか
```

を決めてはいけない。

決めてよいのは:

```text
resourceをいつ破棄できるか
Vk commandをどうsubmitするか
surfaceをどう作るか
```

だけ。

---

# 18. Sapphireからそのまま持ってくる対象

## そのまま使用

```text
MelonInstance FrameLatch free-function closure
MelonInstance FrameLatch member methods
FrameLatch state members
SoftPackedScreenStats
SoftPackedFrameSnapshot
capture／class4判定
temporal history gate
previous packed carry
screenSwap semantics
VulkanOutput composition decisions
FrameQueue policy／ordering／previous reuse
compositor shader
accumulate shader
```

## Desktop adapterへ限定

```text
ANativeWindow ↔ Qt VkSurfaceKHR
EGL sync ↔ Vulkan timeline／fence
Android GL texture ↔ Desktop VkImage
Vulkan prototypes ↔ volk
Android filesystem ↔ Desktop filesystem
Android logging ↔ Desktop logging
Android lifecycle ↔ Qt surface lifecycle
final CustomHUD pass
```

---

# 19. S75推奨commit分割

## S75-1

```text
Compile SapphireFrameLatchCore once and link the audited object into melonDS
```

## S75-2

```text
Validate published packed and structured pointer identity against live GPU state
```

## S75-3

```text
Remove unused duplicate pointers from SapphireFrameInput
```

## S75-4

```text
Vendor the exact Sapphire FrameQueue selection core
```

## S75-5

```text
Move Desktop generation and timeline ownership into DesktopFrameLifetimeTracker
```

## S75-6

```text
Add FrameQueue upstream-versus-Desktop differential sequence tests
```

## S75-7

```text
Split VulkanOutput platform synchronization hooks from composition decisions
```

## S75-8

```text
Replace regex parity with generated regions or AST comparison
```

## S75-9

```text
Replace placeholder golden metadata with binary Sapphire snapshot fixtures
```

## S75-10

```text
Replace placeholder 120-frame hashes with real per-frame output hashes
```

## S75-11

```text
Build and link the production melonDS target on Linux Debug and Release
```

## S75-12

```text
Build and link the production melonPrimeDS.exe target on Windows MinGW
```

## S75-13

```text
Expand Sapphire parity workflow paths to all integration sources
```

## S75-14

```text
Verify generation manifest and upstream region hashes read-only
```

## S75-15

```text
Add Vulkan cold-start, renderer-switch and fullscreen lifecycle smoke tests
```

---

# 20. Runtime smoke test

これまで報告されてきた症状を
同じintegration test suiteへ入れる。

```text
1. Vulkanを選択した状態でcold start
2. ROM first frameが白画面にならない
3. Software → Vulkan
4. OpenGL → Vulkan
5. Vulkan → OpenGL
6. Vulkan fullscreen ON
7. fullscreen transition中の再入力
8. CustomHUD表示
9. 1x／2x／3x／4x
10. top／bottom physical mapping
11. 2D black保持
12. 120-frame点滅検出
```

各caseで保存:

```text
frame serial
renderer generation
surface generation
front buffer
screen swap
Snapshot hash
prepared composition hash
final top／bottom hash
FrameQueue selected frameId
```

---

# 21. 禁止事項

```text
- 新しいpixel heuristicを追加する
- class4 thresholdをDesktopだけ変更する
- period-2をframe dropで隠す
- placeholderをfixtureと呼ぶ
- field名一覧をgolden outputと呼ぶ
- OBJECT単体buildをfull production buildと呼ぶ
- Windows jobなしでWindows build完了とする
- prepareFrameForPresentation全体をparity除外する
- dispatchCompositor全体をparity除外する
- FrameQueue主要関数をparity除外する
- lifetime未完了時に別frameへすり替える
- sidecarへscreen owner semanticsを戻す
- generated coreと手編集coreを併存させる
- 同じsourceを別compile flagで二重compileする
```

---

# 22. 完了条件

```text
1. SapphireFrameLatchCore objectがbuild treeに1つだけ
2. そのobjectがproduction melonDSへlinkされる
3. FrameLatch generated sourceがpinned upstreamとbyte／AST一致
4. FrameQueue selection sequenceがupstreamと一致
5. Desktop lifetime trackerがselection semanticsを変更しない
6. VulkanOutput composition decision bodyがupstreamと一致
7. platform差は明示hookだけ
8. binary golden fixtureで全Snapshot field一致
9. 実120-frame hashでunexpected period-2 = 0
10. placeholder fixture = 0
11. Linux Debug／Release full binary link成功
12. Windows MinGW full binary link成功
13. Vulkan cold start成功
14. renderer switch成功
15. fullscreen連続切替でfreezeなし
16. CustomHUD final pass表示
17. 1x～4xで2D文字／black semantics正常
18. CI runが監査commit SHAへ紐づく
```

---

# 23. 最終判断

S74によって:

```text
FrameLatch本体をSapphireへ戻す
```

という主要目的はほぼ達成された。

これは前回からの大きな改善であり、
今後FrameLatchへ独自修正を積み増すべきではない。

ただし現状は:

```text
Sapphire-generated FrameLatch
+
Desktop-forked FrameQueue state machine
+
重要関数を除外したVulkanOutput parity
+
placeholder golden／flicker fixture
+
OBJECT-only Linux build
```

である。

したがってpipeline全体について:

```text
Sapphireと同じ実装
違いはAndroidかDesktopかだけ
```

とはまだ認定できない。

S75の中心は新しいrenderer実装ではない。

```text
既にあるSapphire algorithmをさらにそのまま使用し、
Desktop固有処理をlifetime／WSI adapterへ追い出す
```

こと。

特に次の3点を最優先とする。

```text
1. audited object == production linked object
2. FrameQueue selection core == Sapphire
3. 実frame fixtureでAndroid／Desktop出力一致
```

ここまで完了すれば、
Vulkan 2D pipelineをSapphire exactと判断できる。

---

# 24. 進捗

| Phase | Commit | Status | Notes |
|-------|--------|--------|-------|
| S75-1 | `b510d7f32` | done | Link audited `sapphire_frame_latch_core` object once into melonDS |
| S75-2 | `09d45a481` | done | Validate published packed/structured pointer identity against live GPU |
| S75-3 | `aadb619bf` | done | Remove unused duplicate pointers from `SapphireFrameInput` |
| S75-4 | `1841c8dbd` | done | Vendor exact Sapphire FrameQueue selection core |
| S75-5 | `c8da1ed97` | done | Move Desktop generation/timeline ownership into DesktopFrameLifetimeTracker |
| S75-6 | pending | in progress | FrameQueue upstream-vs-Desktop differential sequence tests |
