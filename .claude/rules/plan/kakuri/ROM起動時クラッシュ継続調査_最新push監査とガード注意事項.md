# ROM起動時クラッシュ継続調査  
## 最新push監査・残存クラッシュの切り分け・共通コードのガード注意事項

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**確認時HEAD:** `facd2ec88775ea1e2fca7651a92114d8274b210b`  
**HEAD commit message:** `Fix`  
**比較元:** `8639ce9e892ed79b0837a97263d48db629ee9ce5`

---

## 1. 現在の再現結果

最新push後の実機確認結果は次のとおり。

| 操作 | 結果 |
|---|---|
| ROM未起動状態で映像設定を開く | 正常 |
| 映像設定からSoftwareを選択する | クラッシュしなくなった |
| 映像設定からVulkanを選択する | クラッシュしなくなった |
| ROMを開く | 依然としてクラッシュする |

したがって、今回のpushによって以下は修正されたと判断できる。

```text
ROM未起動
→ EmuInstance::nds == nullptr
→ msg_ApplyVideoBackendSwitch
→ *emuInstance->nds
→ null pointer dereference
```

一方、ROM起動時クラッシュは残っている。

---

## 2. 最新pushの変更概要

比較範囲:

```text
8639ce9e892ed79b0837a97263d48db629ee9ce5
    ↓
facd2ec88775ea1e2fca7651a92114d8274b210b
```

変更された主なファイル:

```text
src/frontend/qt_sdl/EmuThread.cpp
src/GPU.cpp
src/GPU.h
.claude/skills/build-mingw-vulkan-existing.bat
.claude/rules/plan/ROM起動および映像設定Renderer選択時クラッシュ_追加調査結果.md
.claude/rules/plan/Vulkan選択時に即時クラッシュする問題_調査結果.md
```

runtimeに直接関係する変更は主に次の3ファイル。

```text
src/frontend/qt_sdl/EmuThread.cpp
src/GPU.cpp
src/GPU.h
```

---

## 3. 映像設定変更クラッシュの修正確認

### 3.1 追加されたnull guard

`msg_ApplyVideoBackendSwitch`へ、次の処理が追加されている。

```cpp
if (emuInstance->nds == nullptr)
{
    // No console exists yet. Commit only the staged presentation state.
    // The actual renderer transaction runs after NDS creation because
    // updateVideoRenderer() forces lastVideoRenderer = -1.
    videoBackend = stagedPresentation;
    videoSettingsDirty = true;
    lastVideoRenderer = -1;

#if defined(MELONPRIME_ENABLE_VULKAN)
    auto& session = emuInstance->vulkanFrontendSession();
    session.completeBackendSwitch(
        videoBackend
            == MelonPrime::VideoBackend::PresentationBackend::Vulkan);

    if (videoBackend
        != MelonPrime::VideoBackend::PresentationBackend::Vulkan)
    {
        session.shutdown();
    }
#endif

    msgResult = static_cast<int>(videoBackend);
    break;
}
```

この処理により、ROM未起動状態では次を行わない。

```text
CreateRendererForSelection(*emuInstance->nds, ...)
GPU::SetRenderer(...)
GPU3D::SetCurrentRenderer(...)
renderer settings適用
runtime renderer capability評価
```

ROM未起動時にはpresentation stateだけを更新し、実renderer生成をNDS作成後へ遅延する。

実機でSoftware／Vulkan選択時のクラッシュが消えたため、この修正は有効だったと判断できる。

---

## 4. ROM起動時向けに入れた修正

### 4.1 `GPU::SetRenderer()`のnull guard

修正前:

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    SyncAllVRAMCaptures();

    ...
}
```

修正後:

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    if (Rend)
        SyncAllVRAMCaptures();

    ...
}
```

初回`GPU` constructorでは`Rend == nullptr`であるため、旧rendererのcapture同期を実行しなくなった。

### 4.2 `VRAMCaptureBlockFlags`のゼロ初期化

修正前:

```cpp
u16 VRAMCaptureBlockFlags[16];
```

修正後:

```cpp
u16 VRAMCaptureBlockFlags[16] {};
```

未初期化値をcapture metadataとして読み込む可能性を除去した。

---

## 5. 前回のROM起動クラッシュ仮説の更新

前回は次をROM起動時クラッシュの最有力原因としていた。

```text
new NDS
→ GPU constructor
→ SetRenderer(nullptr)
→ SyncAllVRAMCaptures()
→ 未初期化VRAMCaptureBlockFlags
→ nullのRendを参照
→ crash
```

最新pushにより、この経路には次の両方の対策が入った。

```text
if (Rend)
    SyncAllVRAMCaptures();

VRAMCaptureBlockFlags[16] {};
```

しかし、実機ではROM起動時クラッシュが残っている。

したがって、判定を次のように更新する。

| 項目 | 更新後の判定 |
|---|---|
| 未初期化`VRAMCaptureBlockFlags` | 実在する不具合 |
| 初回`SetRenderer()`での無条件capture同期 | 実在する危険な処理 |
| 今回報告されているROM起動クラッシュの主原因 | **否定または少なくとも単独原因ではない** |
| 修正を残す価値 | あり。未定義動作除去として有効 |
| 次に調査すべき箇所 | NDS作成後から最初のemulation frameまで |

前回文書にある「ROM起動クラッシュの最有力原因」という表現は、現在の実機結果に合わせて撤回する必要がある。

---

# 6. 重要注意事項  
## 共通コードを変更しているが、`MELONPRIME_DS`ガードがない

今回のpushでは、次の2変更がmelonDS共通コードへ無条件で入っている。

### 6.1 `src/GPU.cpp`

```cpp
if (Rend)
    SyncAllVRAMCaptures();
```

この変更は、次のガードで囲まれていない。

```cpp
#ifdef MELONPRIME_DS
...
#endif
```

### 6.2 `src/GPU.h`

```cpp
u16 VRAMCaptureBlockFlags[16] {};
```

この変更も、次のガードで囲まれていない。

```cpp
#ifdef MELONPRIME_DS
...
#endif
```

### 6.3 `EmuThread.cpp`側はガード内

一方、`msg_ApplyVideoBackendSwitch`自体は既存の次のブロック内にある。

```cpp
#ifdef MELONPRIME_DS
case msg_ApplyVideoBackendSwitch:
    ...
#endif
```

さらにVulkan session操作は次で保護されている。

```cpp
#if defined(MELONPRIME_ENABLE_VULKAN)
...
#endif
```

したがって、映像設定変更向けnull guardはMelonPrime専用差分として隔離されている。

---

## 7. upstreamとの差分

確認時点のupstream `melonDS-emu/melonDS`では、`GPU::SetRenderer()`は次のまま。

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    SyncAllVRAMCaptures();

    ...
}
```

また、`VRAMCaptureBlockFlags`も次のまま。

```cpp
u16 VRAMCaptureBlockFlags[16];
```

つまり今回の2変更は、確認時点ではupstreamに存在しない。

```text
if (Rend)
    SyncAllVRAMCaptures();

u16 VRAMCaptureBlockFlags[16] {};
```

そのため、次回upstream同期時にはmelonPrime固有差分として検出される。

---

## 8. この2変更を単純に`#ifdef MELONPRIME_DS`へ入れるべきか

### 結論

**注意は必要だが、機械的にガードを追加するのも適切ではない。**

理由は、この2変更がMelonPrime固有機能ではなく、一般的な初期化安全性の修正だから。

```cpp
if (Rend)
    SyncAllVRAMCaptures();
```

は「旧rendererが存在する場合だけ旧rendererからcaptureを同期する」という一般的に妥当な条件。

```cpp
u16 VRAMCaptureBlockFlags[16] {};
```

は未初期化readを防ぐ一般的なゼロ初期化。

これらを次のように囲むと、非MelonPrime buildでは旧動作と未定義動作を意図的に残すことになる。

```cpp
#ifdef MELONPRIME_DS
    if (Rend)
        SyncAllVRAMCaptures();
#else
    SyncAllVRAMCaptures();
#endif
```

```cpp
#ifdef MELONPRIME_DS
    u16 VRAMCaptureBlockFlags[16] {};
#else
    u16 VRAMCaptureBlockFlags[16];
#endif
```

これは差分隔離としては明確だが、core correctnessの観点では不自然。

---

## 9. 推奨する扱い

### 案A: 共通バグ修正として無条件適用を維持する

この場合、コード近辺へ目的を明記する。

```cpp
// Core safety fix: the first GPU::SetRenderer() call occurs before an
// existing renderer has been installed. Do not attempt to synchronize
// captures through a null previous renderer.
if (Rend)
    SyncAllVRAMCaptures();
```

```cpp
// Core safety fix: capture metadata must be deterministic before the
// first renderer installation.
u16 VRAMCaptureBlockFlags[16] {};
```

さらにcommitをVulkan／MelonPrime機能実装とは分離する。

推奨commit単位:

```text
Core: initialize VRAM capture metadata and guard initial renderer install
```

利点:

```text
非MelonPrime buildにも安全性修正が入る
コードの意図が自然
未定義動作を残さない
```

欠点:

```text
upstreamとの差分が増える
「MelonPrime差分はすべてifdef内」という運用規約から外れる
upstream merge時に競合候補になる
```

### 案B: strict isolationを優先する

プロジェクト方針として、upstream共通部分を一切変えたくない場合。

ただし、単純なifdefではなく、MelonPrime固有経路側で初期renderer構築を安全化できる設計へ変更する必要がある。

確認対象:

```text
NDSArgs::Renderer
EmuInstance::updateConsole()
GPU constructor
GPU::SetRenderer()
```

現状は`SoftRenderer` constructorが`NDS&`を要求するため、`NDSArgs`作成時に単純にSoftware rendererを先行生成することはできない。

したがってstrict isolationを行う場合でも、設計変更が必要。

### 案C: upstreamへ一般修正として提案する

最も整理しやすい扱い。

```text
1. この2変更をcore safety fixとして独立commit化
2. MelonPrime固有実装と分離
3. upstreamへ同等修正を提案
4. upstreamへ採用された後は独自差分を消す
```

---

## 10. 今後の必須レビュー規約

共通ファイルを変更するときは、次を必ず確認する。

```text
src/GPU.cpp
src/GPU.h
src/GPU3D.cpp
src/GPU3D.h
src/NDS.cpp
src/NDS.h
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/EmuInstance.cpp
```

各変更について、次のどれかを明示する。

```text
A. #ifdef MELONPRIME_DS内のMelonPrime専用変更
B. #if MELONPRIME_ENABLE_*内のbackend専用変更
C. upstreamにも成立するcore correctness fix
D. upstream由来の変更
```

分類できない変更はpush前に止める。

### 推奨コメント形式

```cpp
#ifdef MELONPRIME_DS
// MelonPrime-only: ...
#endif
```

または:

```cpp
// Core safety fix, intentionally shared with non-MelonPrime builds:
// ...
```

### 推奨commit分離

悪い例:

```text
Fix Vulkan startup crash
```

この1 commit内に以下が混在する。

```text
Vulkan専用修正
MelonPrime frontend修正
melonDS core共通修正
build script修正
調査MD更新
```

良い例:

```text
Core: guard initial GPU renderer installation
MelonPrime: defer backend switch until NDS exists
Build: validate Vulkan-enabled MinGW cache
Docs: update renderer crash investigation
```

---

# 11. 残っているROM起動クラッシュの次段階調査

現在の証拠だけでは、faulting instructionは確定していない。

次は、ROM起動処理を段階ログで分割する必要がある。

---

## 12. 最優先の切り分け  
## SoftwareとVulkanを完全に分離する

### Test A: Software固定

起動前のconfigを明示的に次へ設定する。

```text
3D.Renderer = Software
Screen.UseGL = false
```

映像設定画面で選択しただけではなく、保存されたconfig値と起動時ログで確認する。

期待される経路:

```text
new NDS
→ initial SoftRenderer
→ updateVideoRenderer()
→ CreateRendererForSelection(Software)
→ new SoftRenderer
→ RunFrame()
```

### Test B: Vulkan固定

```text
3D.Renderer = Vulkan
Screen.UseGL = false
```

期待される経路:

```text
new NDS
→ initial SoftRenderer
→ updateVideoRenderer()
→ CreateRendererForSelection(Vulkan)
→ VulkanRenderer3D::Init()
→ VulkanFrontendSession::initialize()
→ first RunFrame()
→ submitVulkanFrontendFrame()
```

### 判定

| 結果 | 調査対象 |
|---|---|
| Softwareでも落ちる | NDS／GPU共通起動、MelonPrime OnEmuStart、最初のRunFrame |
| Vulkanだけ落ちる | Vulkan renderer初期化、frontend session、surface／presenter、first submit |
| Softwareは動きVulkanだけ落ちる | 共通GPU constructor説は除外 |
| 両方とも同じ位置で落ちる | renderer選択より前の共通処理 |

---

## 13. 追加すべき段階ログ

### 13.1 `EmuInstance::updateConsole()`

対象:

```text
src/frontend/qt_sdl/EmuInstance.cpp
```

追加位置:

```cpp
Log(Info, "[RomBootTrace] updateConsole begin nds=%p\n", nds);

Log(Info, "[RomBootTrace] BIOS loaded\n");
Log(Info, "[RomBootTrace] firmware loaded\n");

Log(Info, "[RomBootTrace] renderLock acquired\n");

Log(Info, "[RomBootTrace] new NDS begin\n");
Log(Info, "[RomBootTrace] new NDS complete nds=%p\n", nds);

Log(Info, "[RomBootTrace] initial NDS Reset begin\n");
Log(Info, "[RomBootTrace] initial NDS Reset complete\n");

Log(Info, "[RomBootTrace] updateVideoRenderer queued\n");

Log(Info, "[RomBootTrace] cart installation begin\n");
Log(Info, "[RomBootTrace] cart installation complete\n");

Log(Info, "[RomBootTrace] renderLock release\n");
Log(Info, "[RomBootTrace] updateConsole complete\n");
```

### 13.2 `msg_BootROM`

対象:

```text
src/frontend/qt_sdl/EmuThread.cpp
```

追加位置:

```cpp
Log(Info, "[RomBootTrace] msg_BootROM begin\n");
Log(Info, "[RomBootTrace] loadROM begin\n");
Log(Info, "[RomBootTrace] loadROM complete nds=%p\n", emuInstance->nds);
Log(Info, "[RomBootTrace] ResetRuntimeStateForBoot begin\n");
Log(Info, "[RomBootTrace] ResetRuntimeStateForBoot complete\n");
Log(Info, "[RomBootTrace] nds->Start begin\n");
Log(Info, "[RomBootTrace] nds->Start complete\n");
Log(Info, "[RomBootTrace] msg_BootROM complete\n");
```

### 13.3 `msg_EmuRun`

```cpp
Log(Info, "[RomBootTrace] msg_EmuRun begin\n");
Log(Info, "[RomBootTrace] windowEmuStart emitted\n");
Log(Info, "[RomBootTrace] MelonPrime OnEmuStart begin\n");
Log(Info, "[RomBootTrace] MelonPrime OnEmuStart complete\n");
Log(Info, "[RomBootTrace] msg_EmuRun complete\n");
```

### 13.4 `EmuThread::updateRenderer()`

```cpp
Log(Info,
    "[RomBootTrace] updateRenderer begin configured=%d last=%d nds=%p\n",
    configuredRenderer,
    lastVideoRenderer,
    nds);

Log(Info, "[RomBootTrace] CreateRendererForSelection begin\n");
Log(Info, "[RomBootTrace] CreateRendererForSelection complete\n");

Log(Info, "[RomBootTrace] applyRendererCreation begin\n");
Log(Info, "[RomBootTrace] applyRendererCreation complete\n");

Log(Info, "[RomBootTrace] applyRendererSettings begin\n");
Log(Info, "[RomBootTrace] applyRendererSettings complete\n");
```

### 13.5 `applyRendererCreation()`

```cpp
Log(Info, "[RomBootTrace] outer SetRenderer begin\n");
Log(Info, "[RomBootTrace] outer SetRenderer complete\n");

Log(Info, "[RomBootTrace] Renderer3D install begin\n");
Log(Info, "[RomBootTrace] Renderer3D install complete\n");

Log(Info, "[RomBootTrace] frontend session initialize begin\n");
Log(Info, "[RomBootTrace] frontend session initialize complete\n");
```

### 13.6 最初のframe

```cpp
Log(Info, "[RomBootTrace] first RunFrame begin\n");
Log(Info, "[RomBootTrace] first RunFrame complete lines=%u\n", nlines);

Log(Info, "[RomBootTrace] first Vulkan frontend submit begin\n");
Log(Info, "[RomBootTrace] first Vulkan frontend submit complete\n");

Log(Info, "[RomBootTrace] first drawScreen begin\n");
Log(Info, "[RomBootTrace] first drawScreen complete\n");
```

ログはbufferへ残すだけでなく、クラッシュ直前まで確実に保存される出力方法にする。

---

## 14. 次の有力候補

### 候補1: `VulkanRenderer3D::ensureInitialized()`

Vulkan固定時だけ落ちる場合。

確認対象:

```text
VulkanContext::Acquire()
VkDevice取得
shader module作成
descriptor layout作成
pipeline layout作成
graphics pipeline作成
buffer／image allocation
fallback texture upload
```

synthetic validation dispatchは通常`Init()`から外されているが、resource／pipeline初期化自体は残っている。

### 候補2: Vulkan frontend session

確認対象:

```text
MelonPrimeVulkanFrontendSession::initialize()
VulkanOutput::init()
FrameQueue初期化
activePresenter／stagedPresenter状態
beginGeneration()
```

特にROM未起動中にVulkan panelだけ先に作成され、その後NDSが追加されるlifecycleを確認する。

### 候補3: `MelonPrimeCore::OnEmuStart()`

Softwareでも落ちる場合の優先候補。

確認対象:

```text
ResetRuntimeStateForBoot()
OnEmuStart()
ROM checksum検出
address table設定
runtime hook初期化
MainRAM参照
player pointer初期化
```

renderer修正と無関係なため、Softwareでも同じクラッシュが起きる場合はここを疑う。

### 候補4: 最初のrenderer transaction

```text
updateVideoRenderer()
→ videoSettingsDirty = true
→ lastVideoRenderer = -1
→ first frameでupdateRenderer()
→ applyRendererCreation()
```

新しいNDSは最初にSoftware rendererで作られた後、最初のframe直前に選択rendererへ再度差し替えられる。

この二段階初期化をログで確認する。

---

## 15. 追加の設計上の注意

### 15.1 presentationとNDS rendererはlifecycleが異なる

```text
ScreenPanelVulkan
→ ROMなしでも存在可能

NDS／GPU／Renderer3D
→ ROMまたはfirmware起動後に存在
```

このため、次を混同しない。

```text
Vulkan panelの初期化成功
Vulkan Renderer3Dの初期化成功
VulkanOutputの初期化成功
最初のVulkan frame submit成功
swapchain present成功
```

それぞれ別の段階。

### 15.2 「ROMを開いた瞬間」だけでは位置を断定できない

UI上はROM選択直後に見えても、内部では次が連続する。

```text
ROM読込
NDS再構築
NDS Reset
cart設定
msg_EmuRun
MelonPrime OnEmuStart
renderer transaction
first RunFrame
Vulkan frame submit
drawScreen
```

段階ログがない状態では、どこまで到達したか判定できない。

---

## 16. 推奨する次の実装単位

### Commit 1: 診断ログだけ

```text
Debug: add ROM boot stage trace
```

このcommitでは動作変更を入れない。

### Commit 2: ガード方針整理

```text
Core: document initial GPU renderer safety fixes
```

または、strict isolationを採用する場合:

```text
MelonPrime: isolate initial renderer safety behavior
```

### Commit 3: ログで確定した原因だけを修正

```text
Fix: <確定したstage名> ROM boot crash
```

推測修正をさらに積み重ねない。

---

## 17. 現時点の最終判断

### 修正済み

```text
ROM未起動状態でSoftware／Vulkanを選択した際の
emuInstance->nds null dereference
```

### 実在するが主原因ではなかった可能性が高い修正

```text
初回GPU::SetRenderer()での旧renderer capture同期
VRAMCaptureBlockFlagsの未初期化
```

### 未解決

```text
ROMを開いた後、NDS作成開始から最初のframe表示までのどこかで発生するクラッシュ
```

### ガード監査結果

```text
EmuThread.cppの変更
→ MELONPRIME_DSガード内

GPU.cppのif (Rend)
→ 共通コードへ無条件適用

GPU.hのVRAMCaptureBlockFlags[16] {}
→ 共通コードへ無条件適用
```

この2つの共通変更は、一般的なcore safety fixとしては妥当だが、MelonPrime差分隔離規約の観点では明示的なレビューと説明が必要。

**今後は「MelonPrime専用変更」「backend専用変更」「core共通修正」をcommit単位でも分離すること。**
