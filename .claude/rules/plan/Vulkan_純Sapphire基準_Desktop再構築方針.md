# Vulkan実装を純Sapphire基準で再構築する方針

**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**目的:** 現在のVulkan実装を継ぎ足し修正するのではなく、純Sapphireを基準にDesktop版Vulkan実装を再構築する。

---

## 1. 結論

現状では、既存Vulkan実装を修繕し続けるより、**純Sapphireを起点にDesktop版を再構築する方が成功確率は高い**。

ただし、ファイル名に `Vulkan` が付くものを一括削除するのではなく、**Sapphire vendor-first rebase**として進める。

削除基準はファイル名ではなく責務とし、純Sapphireの描画・同期・frame ownershipを残し、Desktop固有差分を最小限のadapterへ限定する。

---

## 2. 作り直した方がよい理由

現在のVulkan実装では、以下が相互に依存している。

- 複数rendererのownership
- Desktop独自FrameLatch
- previous-frame carry
- temporal repair
- capture source補完
- protected-black補正
- 上下画面混入対策
- Engine A/Bの独自cache
- ScreenSwap補正
- FrameQueueの独自temporal state
- Presenter側のprevious resource保持
- 静的文字列ベースのparity test

この状態では、1つの症状を修正すると別の箇所で、

- 激しい点滅
- 黒色透過
- 上下画面混入
- 初回白画面
- frame ownership不整合
- previous/current frame交互表示
- cold-start crash

が再発しやすい。

現実装は、Sapphireをそのまま移植した構造というより、Sapphireを基盤に独自rendererを積み上げた状態に近い。

そのため、今後も局所修正を続けるより、固定した純Sapphireをsource of truthとして再構築した方が早い。

---

## 3. 基本方針

理想構成は以下とする。

```text
Pinned Sapphire core
        │
        ├── AndroidPlatformAdapter
        │
        └── DesktopPlatformAdapter
                 ├── Qt native surface
                 ├── Desktop Vulkan loader
                 ├── Qt window lifecycle
                 └── final presenter destination
```

Android版とDesktop版の差分はplatform adapterに限定する。

Sapphireの以下の挙動は、Android/Desktopで同一にする。

- GPU timing
- DrawScanline順序
- DrawSprites順序
- Framebuffer ownership
- SwapBuffers
- FrameQueue semantics
- capture処理
- ScreenSwap
- 2D/3D合成
- protected black
- frame serial
- producer/consumer同期
- previous-frame source選択
- structured 2D handoff
- class4 state machine

---

## 4. そのまま残すもの

以下は純Sapphireを基準として残す。

- pinned Sapphire原本
- Sapphire GPU2D Unit
- Sapphire SoftRenderer
- Sapphire GPU3D Vulkan renderer
- Sapphire Framebuffer割当
- Sapphire SwapBuffers
- Sapphire FrameQueue
- Sapphire VulkanOutput
- Sapphire capture処理
- Sapphire ScreenSwap処理
- Sapphire frame ownership
- Sapphire producer/consumer同期
- Sapphire previous-frame semantics
- Sapphire composition mode判定
- Sapphire protected-black semantics
- 通常melonDSのCPU、ROM、Audio、Input
- Qtの一般UI
- MelonPrime固有設定
- MelonPrime固有ROM機能
- 最終段のCustom HUD

「Sapphireを参考にして書き直したコード」ではなく、**固定commitのSapphire原本を可能な限り無改変でコンパイルする**。

---

## 5. 一旦外すもの

以下は再構築開始時にproduction pathから外す。

- `MelonPrimeDesktopVulkan*` の独自FrameLatch
- Desktop独自FrameQueue
- previous-frame carry補正
- temporal repair
- Engine A/B独自cache
- capture sourceからの局所補完
- protected-black後付け補正
- 上下画面混入対策heuristic
- per-line repair
- per-pixel repair
- `ActiveGPU2DPath` による実行中の経路切替
- `SapphireGpu2DState` による二重activation管理
- VulkanOutput外部のprevious resource保持
- Presenter側の独自frame history
- Sapphire snapshotへのDesktop固有field追加
- Sapphireコードをコピーして改変した派生実装
- 文字列の存在だけを確認するparity test
- cold-start検証に不要なHUD
- cold-start検証に不要な内部解像度設定
- cold-start検証に不要なfullscreen制御
- cold-start検証に不要なfilter
- live renderer switching

既存コードは即時削除ではなく、現行ブランチに保存し、新しい再構築ブランチでは参照しない構成にする。

---

## 6. ブランチ戦略

現在の状態は必ず保存する。

```text
highres_fonts_v3
    現状保存

vulkan_sapphire_desktop_rebuild
    純Sapphire基準の再構築用
```

必要に応じて現HEADへtagも付与する。

例:

```text
vulkan-pre-sapphire-rebuild
```

旧実装は、再構築時のsource of truthにはしない。

利用目的は以下に限定する。

- Desktop WSI実装の参照
- Qt surface生成の参照
- 設定UIの参照
- Custom HUDの参照
- 回帰ログの比較
- crash RVAの比較

---

## 7. AndroidとDesktopで許可する差分

以下のみplatform差分として許可する。

```text
Android                         Desktop
ANativeWindow                   Qt native window / HWND / XCB / Wayland
Android Vulkan loader           volk / Desktop Vulkan loader
Android VkSurfaceKHR            Win32 / XCB / Wayland / Metal surface
Activity lifecycle              Qt window lifecycle
Android cache path              Desktop filesystem path
logcat                          stdout / file log / crash dump
Android input/layout            Qt input/layout
```

### 許可する差分

- WSI
- Vulkan loader
- native window取得
- surface生成
- filesystem
- logging
- lifecycle eventの入口
- final window layout
- Custom HUDの最終pass

### 許可しない差分

- snapshot ABI
- capture class
- composition mode
- protected-black semantics
- line metadata
- ScreenSwap semantics
- history gate
- previous source selection
- class4 state machine
- structured plane handoff
- capture owner
- FrameQueue temporal requirements
- frame completion条件
- GPU timing
- renderer ownership

---

## 8. 再構築フェーズ

### Phase 0 — 現状凍結

実施内容:

- 現HEADをbranchまたはtagで保存
- 現在のbinaryを保存
- crash dumpを保存
- runtime logを保存
- 問題が再現するROMと設定を固定
- 現在のVulkan関連dependency graphを作成
- 現在のframe lifecycleを図示
- 現在のcrash RVAを記録
- 現在の点滅動画を保存
- 現在のstage hashを保存

目的:

再構築後に、同じ不具合が消えたかを比較可能にする。

### Phase 1 — 純Sapphire coreのみ

この段階では以下を含めない。

- Qt presenter
- Custom HUD
- fullscreen
- scaling
- filter
- temporal repair
- Desktop独自FrameLatch
- previous-frame補完
- 独自capture補完
- live renderer switching

実施内容:

- pinned Sapphire sourceをvendorとして配置
- Sapphire GPU2Dをそのまま使用
- Sapphire SoftRendererをそのまま使用
- Sapphire FrameQueueをそのまま使用
- Sapphire VulkanOutputをそのまま使用
- Sapphire snapshot ABIをそのまま使用
- Sapphire frame completion contractをそのまま使用
- synthetic testまたは最小ROMで `RunFrame()` 完了を確認

完了条件:

```text
RunFrame complete
DrawScanline complete
DrawSprites complete
SwapBuffers complete
frame published
no crash
```

ここで失敗した場合、原因は以下に限定できる。

- ABI不整合
- missing dependency
- melonDS core version差
- build flag差
- compiler差
- Vulkan loader差

### Phase 2 — 最小Desktop Vulkan surface

この段階ではまだSapphire frameをpresentしない。

実装対象:

```text
VkInstance
VkPhysicalDevice
VkDevice
VkQueue
VkSurfaceKHR
VkSwapchainKHR
```

追加するDesktop差分:

- Qt native handle取得
- Win32/XCB/Wayland surface生成
- volk初期化
- swapchain lifecycle
- window resize対応
- surface lost対応

最初の確認は単色clearのみとする。

完了条件:

```text
window create
surface create
swapchain create
acquire
clear
present
resize
shutdown
```

### Phase 3 — Sapphire outputをそのまま接続

実施内容:

- Sapphireで完成したframeを取得
- Desktop側で再合成しない
- Desktop側でsnapshotを改造しない
- Desktop側でprevious/currentを選び直さない
- Sapphire FrameQueueの同期規則を維持
- Sapphire VulkanOutputのresource ownershipを維持
- Desktop presenterは最終出力のみ受け取る

禁止事項:

- CPUへreadbackして再アップロード
- Desktop独自texture再生成
- 上下画面の再判定
- ScreenSwapの再判定
- capture ownerの再推定
- 黒色の再分類
- previous frameの独自選択

完了条件:

```text
Sapphire frame produced
same frame acquired
same frame presented
no frame serial mismatch
no screen swap mismatch
no temporal alternation
```

### Phase 4 — ROM cold-start

条件を固定する。

```text
windowed
1x
HUD off
filter off
fullscreen off
repair off
renderer switching off
```

完了条件:

```text
first RunFrame complete
first frame published
first frame acquired
first frame presented
splash hidden
no crash
no white screen
no flicker
```

この段階では画質よりもframe lifecycleの正常性を優先する。

### Phase 5 — 既存機能を一つずつ戻す

戻す順番:

1. 画面レイアウト
2. ScreenSwap UI
3. fullscreen
4. resize
5. 内部解像度
6. filter
7. Custom HUD
8. renderer切替
9. 詳細設定
10. platform固有最適化

各機能は1commit単位で戻す。

1つのcommitで複数機能を戻さない。

各追加後に以下を必須確認する。

- clean build
- cold-start
- resize
- fullscreen transition
- 120-frame capture
- period-2 flicker検出
- top/bottom golden image
- frame serial連続性
- resource leak
- shutdown

---

## 9. source of truthの統一

1フレームについて、以下は1回だけ確定する。

```cpp
struct SapphireFrameInput
{
    Frame* frame;
    int frontBuffer;
    bool preparedFrameScreenSwap;

    const u32* packedTop;
    const u32* packedBottom;

    const StructuredPlane* structuredTop;
    const StructuredPlane* structuredBottom;

    const Capture3DSource* capture3dSource;
    const CaptureLineMask* captureLineMask;
};
```

この入力は、producer frame完了後に1回だけ生成する。

以下の処理はすべて同じimmutable inputを使用する。

- FrameLatch
- FrameQueue
- VulkanOutput
- compositor
- presenter

禁止事項:

- latch後にlive GPU stateを再参照
- output準備時にfront bufferを再取得
- presenter側でScreenSwapを再評価
- current frameとprevious frameを部分的に混在
- published metadataとlive metadataの混在

---

## 10. temporal処理の扱い

production pathは純Sapphireと同一にする。

debug用にtemporal処理を無効化する場合は、全層で一括無効化する。

無効化対象:

- FrameLatch previous carry
- history gate
- FrameQueue previous reuse
- VulkanOutput previous top source
- VulkanOutput previous bottom source
- previous renderer snapshot
- previous capture3d source
- class4 persistent state
- pending previous resource reference
- presenter frame history

禁止される状態:

```text
FrameLatch      current-only
FrameQueue      temporal enabled
VulkanOutput    previous source enabled
Presenter       history retained
```

このようなpartial disableは、current frameとprevious frameの交互表示を起こし、激しい点滅の原因になる。

---

## 11. Sapphire snapshot ABI

Sapphireの `SoftPackedFrameSnapshot` は固定する。

Desktop固有の以下の情報をsnapshotへ追加しない。

- sourceFrameSerial
- rendererGeneration
- hardwareScreenSwap
- physical top engine
- physical bottom engine
- Desktop capture metadata
- Qt lifecycle metadata
- presenter generation

Desktop固有metadataはsidecarへ分離する。

```cpp
struct DesktopFrameProvenance
{
    u64 publishedFrameSerial;
    u64 rendererGeneration;
    u64 presenterGeneration;
    int physicalTopEngine;
    int physicalBottomEngine;
};
```

sidecarはvalidationとloggingにのみ使用する。

composition semanticsには使用しない。

---

## 12. Sapphireコードの取り込み方法

Sapphire FrameLatchやVulkanOutputを手作業で再実装しない。

推奨構成:

```text
src/frontend/qt_sdl/VulkanReference/
    pinned Sapphire source

src/frontend/qt_sdl/SapphireGenerated/
    generated Desktop-compatible source

tools/generate_sapphire_vulkan_sources.py
```

generatorで許可する変換:

- namespace変更
- include path変更
- class名変更
- member access adapter
- source accessor adapter
- platform logging
- Android WSI呼び出しの除去
- Desktop WSI adapter接続

generatorで禁止する変換:

- algorithm変更
- if条件追加
- threshold変更
- capture分類変更
- ownership変更
- previous-frame選択変更
- ScreenSwap判定変更
- protected-black変更
- class4状態遷移変更
- line metadata変更
- snapshot field追加

CIでは、pinned sourceとgenerated sourceのnormalized diffを検証する。

---

## 13. テスト方針

文字列の存在だけを確認するstatic testでは不十分。

必須テスト:

### 13.1 Generated-source verification

- pinned Sapphire source hash確認
- generator再実行
- generated diffなし
- unauthorized transformなし

### 13.2 Clean build

- Windows clean Vulkan build
- Linux X11 clean Vulkan build
- Linux Wayland clean Vulkan build
- Debug build
- Release build

### 13.3 Android/Desktop golden parity

同一ROM、同一frame、同一設定で以下を比較する。

- packed top
- packed bottom
- structured top
- structured bottom
- capture masks
- prepared GPU buffers
- final compositor inputs
- final output

### 13.4 120-frame flicker test

120frameを連続取得し、period-2 alternationを検出する。

検査対象:

- full-frame hash
- top-screen hash
- bottom-screen hash
- capture-area hash
- background-only hash
- sprite-only hash

異常例:

```text
A B A B A B
```

静止画面でこの交互hashが出た場合は失敗とする。

### 13.5 lifecycle test

- cold-start
- ROM switch
- renderer switch
- resize
- minimize/restore
- fullscreen enter/exit
- surface lost
- device lost simulation
- shutdown

---

## 14. 診断ログ

各presented frameについて、同一行または同一transaction IDで以下を記録する。

```text
presentedFrameId
emulatedFrameSerial
publishedFrameSerial
rendererGeneration
presenterGeneration
frontBuffer
preparedFrameScreenSwap
snapshotScreenSwap
previousSnapshotScreenSwap
screenSwapToggledFromPrevious
historyGateActive
previousTopSourceValid
previousBottomSourceValid
capture3dSourceValid
liveSourceScreenSwap
```

frame stageごとのhashも記録する。

```text
raw published packed
latched packed
prepared GPU buffers
compositor inputs
final output
```

これにより、どのstageでA/B交互化したかを特定できる。

---

## 15. コミット分割案

```text
R1  Preserve current Vulkan branch and diagnostics
R2  Vendor pinned Sapphire source
R3  Add Sapphire source generator
R4  Restore exact Sapphire snapshot ABI
R5  Build pure Sapphire GPU2D core
R6  Build pure Sapphire VulkanOutput and FrameQueue
R7  Add minimal Desktop Vulkan WSI adapter
R8  Present solid-color Desktop swapchain
R9  Connect exact Sapphire output
R10 Add atomic SapphireFrameInput
R11 Add provenance sidecar validation
R12 Add clean-build CI
R13 Add Android/Desktop golden parity
R14 Add 120-frame flicker detector
R15 Restore Qt screen layout
R16 Restore fullscreen and resize
R17 Restore internal resolution
R18 Restore Custom HUD
R19 Restore renderer switching
R20 Remove obsolete Desktop Vulkan implementation
```

---

## 16. 完了条件

再構築完了とみなす条件:

- production pathが純Sapphire相当
- Android/Desktop差分がplatform adapterのみ
- Sapphire snapshot ABIが完全一致
- Sapphire FrameQueue semanticsが完全一致
- Sapphire VulkanOutput semanticsが完全一致
- 1フレームの入力がatomicに固定される
- FrameLatchとVulkanOutputが同じfront bufferを使用する
- FrameLatchとVulkanOutputが同じScreenSwapを使用する
- partial temporal disableが存在しない
- current/previous frame混在が存在しない
- 120-frame period-2 flickerが0件
- 上下画面混入が0件
- 黒色透過が0件
- cold-start white screenが0件
- Windows clean build成功
- Linux clean build成功
- Android Sapphire golden parity成功
- Custom HUDが最終passとしてのみ追加される
- 独自pixel repairがproduction pathに存在しない
- 独自ownership heuristicがproduction pathに存在しない

---

## 17. 最終判断

現状は「あと1か所直せば完成」という構造ではない。

複数の補正層とownership層が相互に依存しており、局所修正を続けるほど原因追跡が難しくなる。

したがって、以下の方針が妥当である。

1. 現在の実装をbranchまたはtagで完全保存する。
2. 新しい再構築ブランチを作成する。
3. 純Sapphireを固定source of truthにする。
4. Desktopとの差分をWSI、loader、lifecycle、filesystem、logging、HUDに限定する。
5. 描画、capture、ScreenSwap、FrameQueue、temporal処理には独自差分を入れない。
6. 機能はcold-start成功後に1つずつ戻す。
7. 最終的に旧Desktop Vulkan実装を削除する。

**既存Vulkan実装を継ぎ足し修正するより、純SapphireからDesktop版を再構築した方が、実装速度・検証可能性・保守性のすべてで有利である。**

---

## 18. 進捗

> **S81監査（2026-07-16）による是正:** commit `fafad722b` "Rebuild Phase 5"
> はDesktop Vulkan実装ファイル削除を一切行わず、`VulkanReference_LegacyCustom/ARCHIVED.md`
> を追加しただけだった（`git show --stat fafad722b` で確認、diff 1ファイル
> +6行のみ）。CMake側でも`MelonPrimeScreenVulkan`/`MelonPrimeVulkanFrontendSession`/
> `MelonPrimeDesktopVulkanPresenter`等の旧統合実装は`MELONPRIME_SAPPHIRE_REBUILD`の
> 値に関わらず今も無条件でproduction pathへlinkされている
> （`src/frontend/qt_sdl/CMakeLists.txt`の`MELONPRIME_VULKAN_ACTIVE`ブロック）。
> Phase 1も`MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN`の既定値が`OFF`のままで、
> "純Sapphire core"と称するビルドが実際には正規化GPU2Dソースを使っていなかった。
> 以下のテーブルはこれらを反映して是正した。詳細:
> [Vulkan_S81_純Sapphire再構築ブランチ監査](Vulkan_S81_純Sapphire再構築ブランチ監査_PostFinishFrameクラッシュ_フェーズ別修正指示.md)

| Phase | 内容 | 状態 | コミット |
|---|---|---|---|
| 0 | 現状凍結（tag/branch/baseline） | **done** | `1360cc76e` |
| 1 | 純Sapphire core（vendor/generator/GPU2D） | **partial** — `MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN`の既定値がOFFのまま | `e95b8d40f` |
| 2 | 最小Desktop WSI + 単色clear | **done** | `b4557998f` |
| 3 | Sapphire output接続 + atomic input | **partial** — 旧Desktop full pipeline（temporal/runtime pacing/resource lease等）が接続されたまま、atomic inputはborrowed raw pointer契約が未確定 | `c640a33c1` |
| 4 | ROM cold-start + CI検証 | **partial** — 起動直後クラッシュとシャットダウン時クラッシュの両方を根本原因特定・修正済み（`docs/vulkan/rebuild/PHASE4_POST_FINISHFRAME_CRASH_ROOT_CAUSE.md`、`PHASE4_COLD_START.md`）。テストはクラッシュせずexit 0まで到達。**S82監査でreopen、S82-0〜S82-4で修正進行中**: `ActiveGPU2DPath`固着修正は真だが、当初はOpenGL/Softwareは未修正のままだった——Vulkan buildではLegacy 2D pathがdead code（`Rend2D_A/B`がcompile時に除外）で、Software走査線縦複製・OpenGL無表示が発生していた。**S82-4（`d90bc05e8`）で根本原因を修正**: `GPU::SetRenderer()`が全backend共通で`Renderer::TakeOwnRenderer3D()`経由でGPU3D ownershipを統一し、Softwareは`ActivateSapphireVulkan2D()`でcanonical Sapphire GPU2D compositorへ接続——`GPU_Soft.cpp`のフレームバッファ共有機構により、Rend2D_A/B・Output2Dの削除なしに走査線縦複製は解消される見込み（実画面での視覚検証は未実施、ヘッドレス環境に画面がないため）。OpenGL/ComputeはS82-6のGLRenderer2D adapter移植待ちで依然dead。`surfacePresent=1`アサーション失敗も未修正（`DesktopFrameLifetimeTracker.cpp`除外によりsurfaceGenerationが常に0、S82-9で対応予定）。CI gateはまだred | `d90bc05e8` *(see PHASE4_COLD_START.md and Vulkan_S82_*.md)* |
| 5 | 機能復元 + 旧実装削除 | **not started** — `ARCHIVED.md`追加のみ、CMakeから旧実装は未削除 | `fafad722b`（誤記あり、上記参照） |

**Tag:** `vulkan-pre-sapphire-rebuild` @ `90bf8333a`  
**Branch:** `vulkan_sapphire_desktop_rebuild`  
**Baseline doc:** `docs/vulkan/rebuild/PHASE0_BASELINE.md`
