# ROM起動時クラッシュ
## 実行ログによるクラッシュ区間確定・Vulkan submit誤スケジューリング調査

**作成日:** 2026-07-15  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**確認時HEAD:** `facd2ec88775ea1e2fca7651a92114d8274b210b`  
**対象実行ファイル:** `melonPrimeDS v3.4.3 v50.exe`  
**build日時:** `2026-07-15 08:20:34 GMT+9`

---

# 1. 結論

今回のログにより、ROM起動クラッシュの位置は大幅に絞り込まれた。

次の処理はすべて正常完了している。

```text
ROM／SAVE読込
BIOS／Firmware読込
NDS object作成
NDS初回Reset
ROM cart挿入
ROM secure area処理
nds->Start()
MelonPrime::OnEmuStart()
Software renderer transaction
最初のNDS::RunFrame()
```

最後に完了しているログ:

```text
[RomBootTrace] first RunFrame complete lines=263
```

その直後:

```text
[RomBootTrace] first Vulkan frontend submit begin
```

でプロセスが終了している。

したがって、以前疑っていた次の処理は今回の直接原因ではない。

```text
GPU constructor
VRAMCaptureBlockFlags初期化
最初のGPU::SetRenderer()
ROM cart設定
MelonPrime OnEmuStart
最初のRunFrame本体
```

現在の最有力箇所は次。

```text
EmuInstance::submitVulkanFrontendFrame()
```

または、診断ログの配置によっては、その直後の:

```text
EmuThread::refreshActualRenderer()
```

である。

ただし、コード上の明確な設計不具合として、**Software／NativeQt動作中にもVulkan frontend submit関数を毎frame無条件で呼んでいる**ことが確認できる。

---

# 2. ログから確定した実行状態

renderer transactionログ:

```text
[RomBootTrace] updateRenderer begin configured=0 last=-1 nds=0000000073e5d040
```

`configured=0`はSoftware。

続いて:

```text
[MelonPrime] video transaction:
requested=Software(0)
normalized=Software(0)
actual=Software(0)
presenter=NativeQt
generation=3
failed_stage=none
reason=none
config_changed=no
```

したがって、クラッシュ時のruntime状態は次。

| 項目 | 状態 |
|---|---|
| requested renderer | Software |
| normalized renderer | Software |
| actual renderer | Software |
| presentation backend | NativeQt |
| Vulkan Renderer3D | 使用対象ではない |
| Vulkan presenter | 現在のpresentation対象ではない |
| 最初のRunFrame | 完走 |
| クラッシュ直前 | Vulkan frontend submit開始 |

これはVulkanを選択した状態でのクラッシュではない。

**Software／NativeQtのframe後にVulkan専用producer処理へ進んだ時点で落ちている。**

---

# 3. 現在のframe loop

`EmuThread::run()`内では、`NDS::RunFrame()`完了後に次を実行している。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// Producer-side snapshot and Vulkan submission stay at the emulation
// frame completion point. The GUI/presenter never reads live GPU state.
emuInstance->submitVulkanFrontendFrame();
refreshActualRenderer();
#endif
```

この条件はcompile-time条件だけである。

```text
MELONPRIME_DSが有効
MELONPRIME_ENABLE_VULKANが有効
```

runtimeのbackendは確認していない。

そのため、Vulkan buildである限り、renderer選択に関係なく毎frame呼ばれる。

---

# 4. Software時に本来期待されるearly return

`EmuInstance::submitVulkanFrontendFrame()`には次のguardがある。

```cpp
void EmuInstance::submitVulkanFrontendFrame()
{
    if (nds == nullptr || !vulkanFrontendSessionOwner)
        return;

    auto& gpu3D = nds->GPU.GPU3D;
    if (!gpu3D.HasCurrentRenderer())
        return;

    auto* renderer3D =
        dynamic_cast<VulkanRenderer3D*>(&gpu3D.GetCurrentRenderer());
    if (renderer3D == nullptr)
        return;

    ...
}
```

Software factoryでは:

```cpp
case renderer3D_Software:
    result.OuterRenderer = std::make_unique<melonDS::SoftRenderer>(nds);
    break;
```

`result.Renderer3D`は生成されない。

`applyRendererCreation()`はそのnullを次へ渡す。

```cpp
nds->GPU.SetRenderer3D(std::move(result.Renderer3D));
```

したがって、正常ならSoftware時は:

```text
gpu3D.HasCurrentRenderer() == false
```

となり、Vulkan sessionへ触れる前にreturnするはず。

それでも関数完了まで到達しない場合、次のどれかが起きている。

```text
1. HasCurrentRenderer()評価前後でhost objectが破損している
2. GPU3D::CurrentRendererが想定外にnon-null
3. CurrentRendererがdangling pointer
4. EmuInstance／GPU3Dのclass layoutにtranslation unit間不整合がある
5. 診断ログの「complete」がsubmit後ではなくrefreshActualRenderer後に配置されている
6. submit自体はreturnし、その直後のrefreshActualRendererで落ちている
7. ログflush前に別threadで落ちている
```

stack traceなしでは、上記の内側までは確定できない。

---

# 5. 最小かつ正しい修正

Vulkan frontend submitは、runtime presentation backendがVulkanのときだけ呼ぶ。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
if (videoBackend
    == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
{
    emuInstance->submitVulkanFrontendFrame();
}
refreshActualRenderer();
#endif
```

これにより、Software／NativeQtではVulkan producer pathへ入らない。

---

# 6. `videoRenderer == renderer3D_Vulkan`でgateしてはいけない

次の実装は避ける。

```cpp
if (videoRenderer == renderer3D_Vulkan)
    emuInstance->submitVulkanFrontendFrame();
```

`EvaluateActualRenderer()`は、Vulkanをactual rendererとして認定するために次を要求する。

```text
Renderer3DReady
Structured2DReady
FinalCompositorReady
FrameQueueReady
SurfaceReady
PresenterReady
```

ところが`Structured2DReady`と`FinalCompositorReady`は、最初のVulkan frameをsubmitするまで成立しない。

そのため、`videoRenderer == Vulkan`をsubmit条件にすると初期化deadlockになる。

正しい条件は:

```cpp
videoBackend == PresentationBackend::Vulkan
```

である。

---

# 7. 推奨する二重防御

## 7.1 scheduler側

```cpp
if (videoBackend
    == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
{
    emuInstance->submitVulkanFrontendFrame();
}
```

## 7.2 submit関数内部

既存guardは残す。

```cpp
if (nds == nullptr || !vulkanFrontendSessionOwner)
    return;

auto& gpu3D = nds->GPU.GPU3D;
if (!gpu3D.HasCurrentRenderer())
    return;

auto* renderer3D =
    dynamic_cast<VulkanRenderer3D*>(&gpu3D.GetCurrentRenderer());
if (renderer3D == nullptr)
    return;
```

scheduler側は「Vulkan処理をVulkan以外で呼ばない」というlifecycle制約。

関数内guardはstale state、fallback、switch途中、teardown競合に対する防御。

---

# 8. 次回ビルドへ追加すべき内部ログ

`submitVulkanFrontendFrame()`内を段階分割する。

```cpp
void EmuInstance::submitVulkanFrontendFrame()
{
    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] entry this=%p nds=%p owner=%p\n",
        static_cast<void*>(this),
        static_cast<void*>(nds),
        static_cast<void*>(vulkanFrontendSessionOwner.get()));

    if (nds == nullptr)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanSubmitTrace] skip: nds=null\n");
        return;
    }

    if (!vulkanFrontendSessionOwner)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanSubmitTrace] skip: owner=null\n");
        return;
    }

    auto& gpu3D = nds->GPU.GPU3D;

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] before HasCurrentRenderer generation=%llu\n",
        static_cast<unsigned long long>(
            gpu3D.GetCurrentRendererGeneration()));

    const bool hasCurrent = gpu3D.HasCurrentRenderer();

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] HasCurrentRenderer=%d\n",
        hasCurrent ? 1 : 0);

    if (!hasCurrent)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanSubmitTrace] skip: no override Renderer3D\n");
        return;
    }

    auto& current = gpu3D.GetCurrentRenderer();

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] current Renderer3D=%p\n",
        static_cast<void*>(&current));

    auto* renderer3D = dynamic_cast<VulkanRenderer3D*>(&current);

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] Vulkan dynamic_cast=%p\n",
        static_cast<void*>(renderer3D));

    if (renderer3D == nullptr)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanSubmitTrace] skip: current renderer is not Vulkan\n");
        return;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] session initialize begin\n");

    const u64 rendererGeneration =
        gpu3D.GetCurrentRendererGeneration();

    if (!vulkanFrontendSessionOwner->initialize(*nds))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[VulkanSubmitTrace] session initialize failed\n");
        return;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] session initialize complete\n");

    vulkanFrontendSessionOwner->beginGeneration(rendererGeneration);

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] capture snapshot begin\n");

    MelonPrimeStructuredSnapshot snapshot{};
    const bool captured =
        MelonPrimeVulkanFrontendSession::captureCompletedSnapshot(
            nds->GPU, rendererGeneration, snapshot);

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] capture snapshot result=%d\n",
        captured ? 1 : 0);

    if (captured)
    {
        const bool submitted =
            vulkanFrontendSessionOwner->submitCompletedFrame(
                *renderer3D, snapshot);

        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanSubmitTrace] submitCompletedFrame result=%d\n",
            submitted ? 1 : 0);
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] exit\n");
}
```

---

# 9. call siteにもruntime状態を出す

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanSubmitTrace] frame gate backend=%s actual=%s(%d)\n",
    MelonPrime::VideoBackend::PresentationBackendName(videoBackend),
    MelonPrime::VideoBackend::RendererName(videoRenderer),
    videoRenderer);

if (videoBackend
    == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
{
    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] gate accepted\n");
    emuInstance->submitVulkanFrontendFrame();
}
else
{
    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanSubmitTrace] gate skipped: non-Vulkan presentation\n");
}

Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanSubmitTrace] refreshActualRenderer begin\n");

refreshActualRenderer();

Platform::Log(
    Platform::LogLevel::Info,
    "[VulkanSubmitTrace] refreshActualRenderer complete\n");
#endif
```

Software時の期待ログ:

```text
frame gate backend=NativeQt actual=Software(0)
gate skipped: non-Vulkan presentation
refreshActualRenderer begin
refreshActualRenderer complete
```

---

# 10. 修正後の判定

## Case A: Softwareで正常起動する

runtime gate追加後にSoftware ROM起動が成功する場合:

```text
Software動作中にVulkan submitを呼んでいたことが
今回のクラッシュ発生条件
```

と判定できる。

## Case B: Softwareでまだ落ちる

次のログ位置で切り分ける。

```text
gate skippedまで出る
refreshActualRenderer beginで止まる
→ refreshActualRenderer／EvaluateActualRenderer

refreshActualRenderer completeまで出る
→ drawScreen

drawScreen completeまで出る
→ frame後処理／window update
```

## Case C: Softwareは動くがVulkanで落ちる

段階ログから次を特定する。

```text
session initialize
beginGeneration
captureCompletedSnapshot
submitCompletedFrame
VulkanOutput::prepareFrameForPresentation
composeAndSubmitFrame
presenter acquire／present
```

---

# 11. ガード監査
## 新たに確認した不整合

前回指摘した共通コード変更に加え、次のguard不整合もある。

## 11.1 `EmuInstance.h`

Vulkan frontend API宣言は:

```cpp
#if defined(MELONPRIME_ENABLE_VULKAN)
MelonPrimeVulkanFrontendSession& vulkanFrontendSession();
void submitVulkanFrontendFrame();
#endif
```

しかし、forward declarationとowner memberは`MELONPRIME_DS`と`MELONPRIME_ENABLE_VULKAN`の二重条件。

宣言側のguardが弱い。

推奨:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
MelonPrimeVulkanFrontendSession& vulkanFrontendSession();
void submitVulkanFrontendFrame();
#endif
```

## 11.2 `EmuInstance.cpp`

includeと関数定義も`MELONPRIME_ENABLE_VULKAN`だけになっている。

推奨:

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
...
#endif
```

## 11.3 `EmuThread.cpp`の関数定義

`EmuThread.h`では次が`#ifdef MELONPRIME_DS`内。

```cpp
MelonPrime::VideoBackend::PresentationBackend
    applyRendererCreation(...);

void applyRendererSettings();
void refreshActualRenderer();
```

しかし`EmuThread.cpp`では、`applyVideoBackendSwitch()`の直後に`#endif`があり、その後のMelonPrime依存関数群が一つの`MELONPRIME_DS` blockで囲まれていない。

少なくとも次のように整理する必要がある。

```cpp
#ifdef MELONPRIME_DS

MelonPrime::VideoBackend::PresentationBackend
EmuThread::applyRendererCreation(...)
{
    ...
}

void EmuThread::applyRendererSettings()
{
    ...
}

void EmuThread::refreshActualRenderer()
{
    ...
}

#endif
```

`updateRenderer()`は:

```cpp
void EmuThread::updateRenderer()
{
#ifdef MELONPRIME_DS
    // MelonPrime transaction
#else
    // upstream melonDS transaction
#endif
}
```

とする。

## 11.4 前回pushの共通コード

以下は引き続き無条件共通変更。

```cpp
// src/GPU.cpp
if (Rend)
    SyncAllVRAMCaptures();
```

```cpp
// src/GPU.h
u16 VRAMCaptureBlockFlags[16] {};
```

一般的なcore safety fixとしては妥当だが、運用上は必ず次のどちらかを明記する。

```text
Core safety fixとして非MelonPrimeにも意図的に適用
```

または:

```text
MelonPrime専用差分としてguard内へ隔離
```

---

# 12. 推奨patch

## 12.1 クラッシュ回避本体

```diff
 #if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
-emuInstance->submitVulkanFrontendFrame();
+if (videoBackend
+    == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
+{
+    emuInstance->submitVulkanFrontendFrame();
+}
 refreshActualRenderer();
 #endif
```

## 12.2 guard整合

```diff
-#if defined(MELONPRIME_ENABLE_VULKAN)
+#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
 MelonPrimeVulkanFrontendSession& vulkanFrontendSession();
 void submitVulkanFrontendFrame();
 #endif
```

`EmuInstance.cpp`のinclude／definitionも同じ条件へ揃える。

---

# 13. 推奨commit分割

```text
Fix: skip Vulkan frontend submission on non-Vulkan presentation backends
```

```text
Build: align MelonPrime Vulkan declarations and definitions under dual guards
```

```text
Debug: add staged Vulkan frontend submission trace
```

core共通変更とは混ぜない。

---

# 14. 現時点の最終判断

今回のログで、ROM起動クラッシュは次の区間へ限定された。

```text
最初のNDS::RunFrame完了
↓
Vulkan frontend submission／actual renderer refresh
↓
drawScreen開始前
```

Software／NativeQtにもかかわらず、compile-time Vulkan有効だけを条件としてVulkan submit関数を呼んでいる。

したがって、最初に行うべき修正は:

```cpp
if (videoBackend == PresentationBackend::Vulkan)
    submitVulkanFrontendFrame();
```

である。

ただし、`submitVulkanFrontendFrame()`内部にはSoftware時にreturnするguardが既にあるため、正確なfaulting instructionを確定するには内部段階ログまたはWindows crash stackが必要。

このruntime gateでSoftware起動が正常化すれば、少なくともクラッシュの発生条件とlifecycle設計ミスは確定する。
